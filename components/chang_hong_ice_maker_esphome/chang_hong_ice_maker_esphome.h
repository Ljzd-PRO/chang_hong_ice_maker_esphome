// Copyright (c) 2026 Ljzd-PRO <me@ljzd.link>
// Project: https://github.com/Ljzd-PRO

#pragma once

#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "power_target_queue.h"

#include <array>
#include <cstdint>
#include <string>

namespace esphome {
namespace chang_hong_ice_maker_esphome {

enum class PanelState : uint8_t {
  UNKNOWN = 0,
  STANDBY,
  RUNNING_LARGE,
  RUNNING_SMALL,
  STARTING,
  STOPPING,
};

class ChangHongIceMakerESPHome : public Component {
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
  std::string fast_state_candidate_text() const;
  std::string feature_state_candidate_text() const;
  std::string action_state_text() const;
  std::string action_result_text() const;
  bool action_busy() const;

  float confidence() const { return this->confidence_; }
  float blink_score() const { return this->blink_score_; }
  float ratio_large_signature() const { return this->ratio_large_signature_; }
  float ratio_mhmhh_signature() const { return this->ratio_mhmhh_signature_; }
  float small_feature_score() const { return this->small_feature_score_; }
  float standby_feature_score() const { return this->standby_feature_score_; }
  float p2_stddev() const { return this->p2_stddev_; }
  float delta_p2_p4() const { return this->delta_p2_p4_; }
  float delta_p5_p2() const { return this->delta_p5_p2_; }
  bool standby_window_valid() const { return this->standby_window_valid_; }
  float raw_p1() const { return this->last_raw_[0]; }
  float raw_p2() const { return this->last_raw_[1]; }
  float raw_p3() const { return this->last_raw_[2]; }
  float raw_p4() const { return this->last_raw_[3]; }
  float raw_p5() const { return this->last_raw_[4]; }

 protected:
  static constexpr uint8_t PIN_COUNT = 5;
  static constexpr uint8_t BIN_COUNT = 64;
  static constexpr uint8_t FAST_WINDOW_BINS = 2;
  static constexpr uint8_t STANDBY_WINDOW_BINS = 32;
  static constexpr uint32_t SAMPLE_INTERVAL_US = 1000;
  static constexpr uint32_t BIN_INTERVAL_MS = 500;
  static constexpr uint32_t FAST_WINDOW_MS = FAST_WINDOW_BINS * BIN_INTERVAL_MS;
  static constexpr uint32_t STANDBY_WINDOW_MS = STANDBY_WINDOW_BINS * BIN_INTERVAL_MS;
  static constexpr uint32_t EVALUATE_INTERVAL_MS = FAST_WINDOW_MS;
  static constexpr uint32_t LOG_INTERVAL_MS = 5000;
  static constexpr uint32_t FIXED_WIFI_SYNC_INTERVAL_MS = 10000;
  static constexpr uint32_t FIXED_WIFI_PREF_KEY = 0x1CE51CE5UL;
  static constexpr uint32_t RECENT_EVENT_VISIBLE_MS = 5000;
  static constexpr uint32_t SHORT_PRESS_COOLDOWN_MS = 800;
  static constexpr uint32_t UV_COOLDOWN_MS = 1500;
  static constexpr uint8_t POWER_QUEUE_MAX = 4;
  static constexpr uint8_t UV_QUEUE_MAX = 4;
  static constexpr float LARGE_SIGNATURE_THRESHOLD = 65.0f;
  static constexpr float MHMHH_SIGNATURE_THRESHOLD = 55.0f;
  static constexpr float STANDBY_P1_MIN_AMPLITUDE = 150.0f;
  static constexpr float STANDBY_P1_MAX_AMPLITUDE = 800.0f;
  static constexpr float STANDBY_MIN_DUTY = 0.08f;
  static constexpr float STANDBY_MAX_DUTY = 0.55f;
  static constexpr float SMALL_P2_STDDEV_MAX = 650.0f;
  static constexpr float SMALL_DELTA_P2_P4_MIN = -120.0f;
  static constexpr float SMALL_DELTA_P5_P2_MAX = 80.0f;
  static constexpr float STANDBY_P2_STDDEV_MIN = 700.0f;
  static constexpr float STANDBY_DELTA_P2_P4_MAX = -145.0f;
  static constexpr float STANDBY_DELTA_P5_P2_MIN = 120.0f;
  static constexpr uint8_t FEATURE_SCORE_THRESHOLD = 2;
  static constexpr uint8_t FEATURE_CONFIRM_COUNT = 2;
  static constexpr uint8_t RUNNING_UNKNOWN_LIMIT = 8;
  static constexpr uint8_t RUNNING_STANDBY_LIMIT = 8;
  static constexpr uint8_t STANDBY_UNKNOWN_LIMIT = 8;

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
    std::array<uint64_t, PIN_COUNT> raw_sq_sum{};
    int64_t delta_p2_p4_sum{0};
    int64_t delta_p5_p2_sum{0};
  };

  struct WindowStats {
    uint8_t bins{0};
    uint32_t total{0};
    uint32_t sig_0hhhh{0};
    uint32_t sig_mhmhh{0};
    std::array<uint64_t, PIN_COUNT> raw_sum{};
    std::array<uint64_t, PIN_COUNT> raw_sq_sum{};
    int64_t delta_p2_p4_sum{0};
    int64_t delta_p5_p2_sum{0};
  };

  struct BlinkStats {
    bool valid{false};
    float score{0.0f};
    float amplitude{0.0f};
    float duty{0.0f};
    uint8_t transitions{0};
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
  void service_power_queue_();
  void service_pending_mode_();
  void service_uv_queue_();
  void sample_once_();
  void advance_bin_if_needed_();
  void reset_bin_(uint8_t index);
  void evaluate_();
  WindowStats calculate_window_stats_(uint8_t window_bins) const;
  BlinkStats calculate_standby_blink_stats_(uint8_t window_bins) const;
  void update_fast_candidate_(PanelState feature_candidate);
  PanelState apply_transition_guard_(PanelState classified);
  uint8_t calculate_small_feature_score_(const WindowStats &stats) const;
  uint8_t calculate_standby_feature_score_(const WindowStats &stats) const;
  float calculate_pin_stddev_(const WindowStats &stats, uint8_t pin_index) const;
  float calculate_delta_mean_(int64_t delta_sum, uint32_t total) const;
  void update_exposed_state_(PanelState classified, uint32_t now_ms);
  void log_summary_(bool force = false);
  char bucket_(uint16_t value) const;
  bool signature_is_0hhhh_(const char *signature) const;
  bool signature_is_mhmhh_(const char *signature) const;
  bool optimistic_active_() const;
  bool cooldown_active_() const;
  bool command_active_() const;
  bool dispatch_power_target_(bool target_on);
  bool queued_power_target_(bool *target_on) const;
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
  PanelState fast_state_candidate_{PanelState::UNKNOWN};
  PanelState feature_state_candidate_{PanelState::UNKNOWN};
  PanelState previous_feature_candidate_{PanelState::UNKNOWN};
  uint8_t feature_candidate_count_{0};
  float confidence_{0.0f};
  float blink_score_{0.0f};
  float ratio_large_signature_{0.0f};
  float ratio_mhmhh_signature_{0.0f};
  float small_feature_score_{0.0f};
  float standby_feature_score_{0.0f};
  float p2_stddev_{0.0f};
  float delta_p2_p4_{0.0f};
  float delta_p5_p2_{0.0f};
  bool standby_window_valid_{false};
  float standby_p1_amplitude_{0.0f};
  float standby_p1_duty_{0.0f};
  uint8_t standby_p1_transitions_{0};
  uint8_t running_unknown_windows_{0};
  uint8_t running_standby_windows_{0};
  uint8_t standby_unknown_windows_{0};

  bool pulse_active_{false};
  PulseKind pulse_kind_{PulseKind::NONE};
  uint8_t pulse_pin_index_{0};
  uint32_t pulse_duration_ms_{0};
  uint32_t pulse_ends_ms_{0};
  uint32_t cooldown_until_ms_{0};
  PowerTargetQueue<POWER_QUEUE_MAX> power_target_queue_{};
  uint8_t uv_toggle_queue_{0};

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

}  // namespace chang_hong_ice_maker_esphome
}  // namespace esphome
