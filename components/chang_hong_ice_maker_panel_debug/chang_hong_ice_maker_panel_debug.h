// Copyright (c) 2026 Ljzd-PRO <me@ljzd.link>
// Project: https://github.com/Ljzd-PRO

#pragma once

#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_oneshot.h"
#include "soc/soc_caps.h"

#include <array>
#include <cstdint>
#include <string>

namespace esphome {
namespace chang_hong_ice_maker_panel_debug {

class ChangHongIceMakerPanelDebug : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  bool set_bias_mode(const std::string &mode);

  std::string bias_mode_text() const;
  std::string adc_backend_text() const { return this->adc_backend_text_; }
  std::string adc_calibration_text() const { return this->adc_calibration_text_; }
  std::string signature_text() const { return this->last_signature_; }
  std::string digital_mask_text() const { return this->digital_mask_text_; }
  std::string frame_csv_text() const { return this->frame_csv_; }

  float sample_rate_hz() const { return this->frame_sample_rate_hz_; }
  float target_sample_rate_hz() const {
    return this->adc_continuous_ready_ ? static_cast<float>(CONTINUOUS_SAMPLE_FREQ_HZ) / PIN_COUNT
                                       : 1000000.0f / SAMPLE_INTERVAL_US;
  }
  float ratio_0hhhh() const { return this->frame_ratio_0hhhh_; }
  float ratio_mhmhh() const { return this->frame_ratio_mhmhh_; }
  float ratio_active_hhhh() const { return this->frame_ratio_active_hhhh_; }

  float raw_mean(uint8_t index) const;
  float calibrated_mv_mean(uint8_t index) const;
  float raw_min(uint8_t index) const;
  float raw_max(uint8_t index) const;
  float raw_stddev(uint8_t index) const;
  float digital_duty(uint8_t index) const;
  float edge_count(uint8_t index) const;
  float pair_delta_mean(uint8_t index) const;
  float adc_error_rate() const { return this->frame_adc_error_rate_; }

 protected:
  static constexpr uint8_t PIN_COUNT = 5;
  static constexpr uint8_t PAIR_COUNT = 7;
  static constexpr uint32_t SAMPLE_INTERVAL_US = 500;
  static constexpr uint8_t MAX_SAMPLES_PER_LOOP = 10;
  static constexpr uint32_t CONTINUOUS_SAMPLE_FREQ_HZ = 50000;
  static constexpr uint32_t CONTINUOUS_READ_BYTES = 256;
  static constexpr uint8_t MAX_CONTINUOUS_READS_PER_LOOP = 12;
  static constexpr uint32_t FRAME_INTERVAL_MS = 1000;
  static constexpr uint32_t FRAME_LOG_INTERVAL_MS = 1000;
  static constexpr uint32_t FIXED_WIFI_SYNC_INTERVAL_MS = 10000;
  static constexpr uint32_t FIXED_WIFI_PREF_KEY = 0x1CE51CE5UL;
  static constexpr adc_atten_t ADC_ATTEN = ADC_ATTEN_DB_12;
  static constexpr adc_bitwidth_t ADC_BITWIDTH = ADC_BITWIDTH_12;

  enum class BiasMode : uint8_t {
    FLOATING = 0,
    INTERNAL_PULLUP,
    INTERNAL_PULLDOWN,
  };

  struct Accumulator {
    uint32_t samples{0};
    std::array<uint64_t, PIN_COUNT> raw_sum{};
    std::array<uint64_t, PIN_COUNT> raw_sq_sum{};
    std::array<uint16_t, PIN_COUNT> raw_min{};
    std::array<uint16_t, PIN_COUNT> raw_max{};
    std::array<uint32_t, PIN_COUNT> digital_high{};
    std::array<uint32_t, PIN_COUNT> edge_count{};
    std::array<bool, PIN_COUNT> last_digital{};
    bool have_last_digital{false};
    uint32_t sig_0hhhh{0};
    uint32_t sig_mhmhh{0};
    uint32_t sig_active_hhhh{0};
    uint32_t adc_read_errors{0};
    std::array<uint32_t, 32> digital_mask_count{};
    std::array<int64_t, PAIR_COUNT> pair_delta_sum{};
  };

  void setup_adc_();
  bool setup_continuous_adc_();
  bool setup_oneshot_adc_();
  void setup_adc_calibration_();
  void setup_fixed_wifi_preferences_();
  void service_fixed_wifi_preferences_();
  bool load_fixed_wifi_settings_(wifi::SavedWifiSettings *settings);
  bool save_fixed_wifi_settings_(const wifi::SavedWifiSettings &settings);
  static bool fixed_wifi_settings_valid_(const wifi::SavedWifiSettings &settings);
  static bool wifi_value_equals_(const char *lhs, const char *rhs);
  void configure_pins_();
  void drain_continuous_adc_();
  uint16_t read_adc_raw_(uint8_t index);
  void accumulate_current_sample_();
  void sample_once_();
  void finalize_frame_(uint32_t now_ms);
  void reset_accumulator_();
  void log_frame_(uint32_t now_ms);
  char bucket_(uint16_t value) const;
  bool signature_is_0hhhh_(const char *signature) const;
  bool signature_is_mhmhh_(const char *signature) const;
  bool signature_is_active_hhhh_(const char *signature) const;
  const char *bias_mode_to_cstr_(BiasMode mode) const;
  uint8_t mode_mask_index_() const;
  void update_calibrated_means_();

  std::array<uint8_t, PIN_COUNT> pins_{{0, 1, 2, 3, 4}};
  std::array<adc_channel_t, PIN_COUNT> adc_channels_{{
      ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3, ADC_CHANNEL_4}};
  static constexpr std::array<std::array<uint8_t, 2>, PAIR_COUNT> PAIRS{{
      {{4, 0}},  // P5-P1, LED5 ice full
      {{4, 1}},  // P5-P2, LED4 no water
      {{0, 1}},  // P1-P2, SW1 power
      {{0, 2}},  // P1-P3, SW2 select
      {{2, 3}},  // P3-P4, LED3 large ice
      {{1, 3}},  // P2-P4, LED2 small ice
      {{0, 3}},  // P1-P4, LED1 power
  }};

  BiasMode bias_mode_{BiasMode::FLOATING};
  Accumulator accum_{};

  uint32_t last_sample_us_{0};
  uint32_t frame_started_ms_{0};
  uint32_t last_log_ms_{0};
  uint32_t last_fixed_wifi_sync_ms_{0};

  adc_oneshot_unit_handle_t adc_handle_{nullptr};
  adc_continuous_handle_t adc_continuous_handle_{nullptr};
  adc_cali_handle_t adc_cali_handle_{nullptr};
  bool adc_ready_{false};
  bool adc_continuous_ready_{false};
  bool adc_cali_ready_{false};
  std::array<uint8_t, CONTINUOUS_READ_BYTES> continuous_read_buffer_{};
  std::array<adc_continuous_data_t, CONTINUOUS_READ_BYTES / SOC_ADC_DIGI_RESULT_BYTES> continuous_parsed_data_{};
  uint8_t continuous_seen_mask_{0};

  std::array<uint16_t, PIN_COUNT> last_raw_{{0, 0, 0, 0, 0}};
  std::array<float, PIN_COUNT> frame_raw_mean_{{0, 0, 0, 0, 0}};
  std::array<float, PIN_COUNT> frame_mv_mean_{{0, 0, 0, 0, 0}};
  std::array<float, PIN_COUNT> frame_raw_min_{{0, 0, 0, 0, 0}};
  std::array<float, PIN_COUNT> frame_raw_max_{{0, 0, 0, 0, 0}};
  std::array<float, PIN_COUNT> frame_raw_stddev_{{0, 0, 0, 0, 0}};
  std::array<float, PIN_COUNT> frame_digital_duty_{{0, 0, 0, 0, 0}};
  std::array<float, PIN_COUNT> frame_edge_count_{{0, 0, 0, 0, 0}};
  std::array<float, PAIR_COUNT> frame_pair_delta_mean_{{0, 0, 0, 0, 0, 0, 0}};

  float frame_sample_rate_hz_{0.0f};
  float frame_ratio_0hhhh_{0.0f};
  float frame_ratio_mhmhh_{0.0f};
  float frame_ratio_active_hhhh_{0.0f};
  float frame_adc_error_rate_{0.0f};
  uint8_t frame_mode_mask_{0};
  char last_signature_[6]{'x', 'x', 'x', 'x', 'x', '\0'};
  std::string adc_backend_text_{"Arduino analogRead fallback"};
  std::string adc_calibration_text_{"unavailable"};
  std::string digital_mask_text_{"0x00"};
  std::string frame_csv_{"D1,0,Float,0,xxxxx,0x00,0,0,0,0,0,0,0,0,0"};

  ESPPreferenceObject fixed_wifi_pref_{};
  bool fixed_wifi_pref_ready_{false};
  bool fixed_wifi_have_saved_{false};
  wifi::SavedWifiSettings last_fixed_wifi_saved_{};
};

}  // namespace chang_hong_ice_maker_panel_debug
}  // namespace esphome
