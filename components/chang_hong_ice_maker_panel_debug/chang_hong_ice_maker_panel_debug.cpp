// Copyright (c) 2026 Ljzd-PRO <me@ljzd.link>
// Project: https://github.com/Ljzd-PRO

#include "chang_hong_ice_maker_panel_debug.h"

#include "esphome/core/log.h"

#include <Arduino.h>
#include "esp_adc/adc_cali_scheme.h"
#include "esp_err.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace esphome {
namespace chang_hong_ice_maker_panel_debug {

static const char *const TAG = "chang_hong_ice_maker_panel_debug";

void ChangHongIceMakerPanelDebug::setup() {
  this->frame_started_ms_ = millis();
  this->reset_accumulator_();
  this->setup_fixed_wifi_preferences_();
  this->configure_pins_();
  this->setup_adc_();
  this->last_sample_us_ = micros();

  ESP_LOGI(TAG, "ChangHongIceMaker panel debug firmware starting");
  ESP_LOGI(TAG, "GPIO mapping: P1=GPIO0 P2=GPIO1 P3=GPIO2 P4=GPIO3 P5=GPIO4");
  ESP_LOGI(TAG, "Sampling: ADC + digital target=%u us, max burst=%u frames, frame=%u ms", SAMPLE_INTERVAL_US,
           MAX_SAMPLES_PER_LOOP, FRAME_INTERVAL_MS);
  ESP_LOGI(TAG, "ADC backend: %s", this->adc_backend_text_.c_str());
  ESP_LOGI(TAG, "ADC calibration: %s", this->adc_calibration_text_.c_str());
  ESP_LOGI(TAG, "ADC config: unit=ADC1 attenuation=12dB bitwidth=12");
  ESP_LOGI(TAG, "Buckets: 0<100, H>3995, M=1500..2500, x=other");
  ESP_LOGW(TAG, "Debug firmware is passive by default. Bias mode starts as Float; no output pulses are implemented.");
}

void ChangHongIceMakerPanelDebug::dump_config() {
  ESP_LOGCONFIG(TAG, "Chang Hong Ice Maker Panel Debug");
  ESP_LOGCONFIG(TAG, "  P1: GPIO%u", this->pins_[0]);
  ESP_LOGCONFIG(TAG, "  P2: GPIO%u", this->pins_[1]);
  ESP_LOGCONFIG(TAG, "  P3: GPIO%u", this->pins_[2]);
  ESP_LOGCONFIG(TAG, "  P4: GPIO%u", this->pins_[3]);
  ESP_LOGCONFIG(TAG, "  P5: GPIO%u", this->pins_[4]);
  ESP_LOGCONFIG(TAG, "  ADC backend: %s", this->adc_backend_text_.c_str());
  ESP_LOGCONFIG(TAG, "  ADC calibration: %s", this->adc_calibration_text_.c_str());
  ESP_LOGCONFIG(TAG, "  Bias mode: %s", this->bias_mode_to_cstr_(this->bias_mode_));
}

void ChangHongIceMakerPanelDebug::loop() {
  uint32_t now_us = micros();
  const uint32_t now_ms = millis();
  this->service_fixed_wifi_preferences_();

  if (this->adc_continuous_ready_) {
    this->drain_continuous_adc_();
  } else {
    uint8_t samples_this_loop = 0;
    while (static_cast<uint32_t>(now_us - this->last_sample_us_) >= SAMPLE_INTERVAL_US &&
           samples_this_loop < MAX_SAMPLES_PER_LOOP) {
      this->last_sample_us_ += SAMPLE_INTERVAL_US;
      this->sample_once_();
      samples_this_loop++;
      now_us = micros();
    }
    if (samples_this_loop == MAX_SAMPLES_PER_LOOP &&
        static_cast<uint32_t>(now_us - this->last_sample_us_) >= SAMPLE_INTERVAL_US) {
      this->last_sample_us_ = now_us;
    }
  }

  if (static_cast<uint32_t>(now_ms - this->frame_started_ms_) >= FRAME_INTERVAL_MS) {
    this->finalize_frame_(now_ms);
    this->frame_started_ms_ = now_ms;
    this->reset_accumulator_();
  }

  if (static_cast<uint32_t>(now_ms - this->last_log_ms_) >= FRAME_LOG_INTERVAL_MS) {
    this->last_log_ms_ = now_ms;
    this->log_frame_(now_ms);
  }
}

bool ChangHongIceMakerPanelDebug::set_bias_mode(const std::string &mode) {
  BiasMode next = BiasMode::FLOATING;
  if (mode == "Float") {
    next = BiasMode::FLOATING;
  } else if (mode == "Internal Pullup") {
    next = BiasMode::INTERNAL_PULLUP;
  } else if (mode == "Internal Pulldown") {
    next = BiasMode::INTERNAL_PULLDOWN;
  } else {
    ESP_LOGW(TAG, "Unknown bias mode '%s'", mode.c_str());
    return false;
  }

  if (next == this->bias_mode_) {
    return true;
  }
  this->bias_mode_ = next;
  this->configure_pins_();
  ESP_LOGW(TAG, "Bias mode changed to %s; this weakly biases all P1-P5 GPIO inputs", this->bias_mode_text().c_str());
  return true;
}

std::string ChangHongIceMakerPanelDebug::bias_mode_text() const {
  return this->bias_mode_to_cstr_(this->bias_mode_);
}

void ChangHongIceMakerPanelDebug::setup_adc_() {
  for (uint8_t i = 0; i < PIN_COUNT; i++) {
    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;
    const esp_err_t err = adc_continuous_io_to_channel(this->pins_[i], &unit, &channel);
    if (err != ESP_OK || unit != ADC_UNIT_1) {
      ESP_LOGW(TAG, "GPIO%u is not ADC1-capable: %s; falling back to Arduino analogRead()", this->pins_[i],
               esp_err_to_name(err));
      this->adc_backend_text_ = "Arduino analogRead fallback";
      return;
    }
    this->adc_channels_[i] = channel;
    ESP_LOGI(TAG, "GPIO%u mapped to ADC1_CH%u", this->pins_[i], static_cast<unsigned>(channel));
  }

  if (!this->setup_continuous_adc_() && !this->setup_oneshot_adc_()) {
    this->adc_backend_text_ = "Arduino analogRead fallback";
    return;
  }

  this->setup_adc_calibration_();
}

bool ChangHongIceMakerPanelDebug::setup_continuous_adc_() {
  adc_continuous_handle_cfg_t handle_config = {};
  handle_config.max_store_buf_size = 4096;
  handle_config.conv_frame_size = CONTINUOUS_READ_BYTES;
  handle_config.flags.flush_pool = true;

  esp_err_t err = adc_continuous_new_handle(&handle_config, &this->adc_continuous_handle_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "adc_continuous_new_handle failed: %s; trying adc_oneshot", esp_err_to_name(err));
    this->adc_continuous_handle_ = nullptr;
    return false;
  }

  adc_digi_pattern_config_t adc_pattern[PIN_COUNT] = {};
  for (uint8_t i = 0; i < PIN_COUNT; i++) {
    adc_pattern[i].atten = ADC_ATTEN;
    adc_pattern[i].channel = static_cast<uint8_t>(this->adc_channels_[i]);
    adc_pattern[i].unit = ADC_UNIT_1;
    adc_pattern[i].bit_width = ADC_BITWIDTH;
  }

  adc_continuous_config_t config = {};
  config.pattern_num = PIN_COUNT;
  config.adc_pattern = adc_pattern;
  config.sample_freq_hz = CONTINUOUS_SAMPLE_FREQ_HZ;
  config.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  config.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;

  err = adc_continuous_config(this->adc_continuous_handle_, &config);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "adc_continuous_config failed: %s; trying adc_oneshot", esp_err_to_name(err));
    adc_continuous_deinit(this->adc_continuous_handle_);
    this->adc_continuous_handle_ = nullptr;
    return false;
  }

  err = adc_continuous_start(this->adc_continuous_handle_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "adc_continuous_start failed: %s; trying adc_oneshot", esp_err_to_name(err));
    adc_continuous_deinit(this->adc_continuous_handle_);
    this->adc_continuous_handle_ = nullptr;
    return false;
  }

  this->adc_ready_ = true;
  this->adc_continuous_ready_ = true;
  this->adc_backend_text_ = "ESP-IDF adc_continuous DMA";
  return true;
}

bool ChangHongIceMakerPanelDebug::setup_oneshot_adc_() {
  adc_oneshot_unit_init_cfg_t unit_config = {
      .unit_id = ADC_UNIT_1,
      .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
      .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  esp_err_t err = adc_oneshot_new_unit(&unit_config, &this->adc_handle_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "adc_oneshot_new_unit failed: %s; falling back to Arduino analogRead()", esp_err_to_name(err));
    return false;
  }

  adc_oneshot_chan_cfg_t channel_config = {
      .atten = ADC_ATTEN,
      .bitwidth = ADC_BITWIDTH,
  };
  for (uint8_t i = 0; i < PIN_COUNT; i++) {
    err = adc_oneshot_config_channel(this->adc_handle_, this->adc_channels_[i], &channel_config);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "adc_oneshot_config_channel GPIO%u/ADC1_CH%u failed: %s; falling back to Arduino analogRead()",
               this->pins_[i], static_cast<unsigned>(this->adc_channels_[i]), esp_err_to_name(err));
      adc_oneshot_del_unit(this->adc_handle_);
      this->adc_handle_ = nullptr;
      return false;
    }
  }

  this->adc_ready_ = true;
  this->adc_backend_text_ = "ESP-IDF adc_oneshot";
  return true;
}

void ChangHongIceMakerPanelDebug::setup_adc_calibration_() {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  adc_cali_curve_fitting_config_t cali_config = {
      .unit_id = ADC_UNIT_1,
      .chan = this->adc_channels_[0],
      .atten = ADC_ATTEN,
      .bitwidth = ADC_BITWIDTH,
  };
  const esp_err_t err = adc_cali_create_scheme_curve_fitting(&cali_config, &this->adc_cali_handle_);
  if (err == ESP_OK) {
    this->adc_cali_ready_ = true;
    this->adc_calibration_text_ = "curve_fitting";
  } else {
    this->adc_calibration_text_ = std::string("unavailable: ") + esp_err_to_name(err);
  }
#else
  this->adc_calibration_text_ = "curve_fitting_not_supported";
#endif
}

void ChangHongIceMakerPanelDebug::setup_fixed_wifi_preferences_() {
  if (global_preferences == nullptr) {
    ESP_LOGW(TAG, "Fixed Wi-Fi credential store unavailable: preferences not initialized");
    return;
  }

  this->fixed_wifi_pref_ = global_preferences->make_preference<wifi::SavedWifiSettings>(FIXED_WIFI_PREF_KEY, true);
  this->fixed_wifi_pref_ready_ = true;

  wifi::SavedWifiSettings settings{};
  if (!this->load_fixed_wifi_settings_(&settings)) {
    ESP_LOGI(TAG, "No fixed Wi-Fi credentials stored yet");
    return;
  }

  this->fixed_wifi_have_saved_ = true;
  this->last_fixed_wifi_saved_ = settings;

  if (wifi::global_wifi_component == nullptr) {
    ESP_LOGW(TAG, "Fixed Wi-Fi credentials found, but Wi-Fi component is unavailable");
    return;
  }

  wifi::WiFiAP sta{};
  sta.set_ssid(settings.ssid);
  sta.set_password(settings.password);
  wifi::global_wifi_component->set_sta(sta);
  ESP_LOGI(TAG, "Restored fixed Wi-Fi credentials for SSID '%s'", settings.ssid);
}

void ChangHongIceMakerPanelDebug::service_fixed_wifi_preferences_() {
  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - this->last_fixed_wifi_sync_ms_) < FIXED_WIFI_SYNC_INTERVAL_MS) {
    return;
  }
  this->last_fixed_wifi_sync_ms_ = now;

  if (!this->fixed_wifi_pref_ready_ || wifi::global_wifi_component == nullptr ||
      !wifi::global_wifi_component->is_connected()) {
    return;
  }

  const wifi::WiFiAP current = wifi::global_wifi_component->get_sta();
  wifi::SavedWifiSettings settings{};
  strncpy(settings.ssid, current.get_ssid().c_str(), sizeof(settings.ssid) - 1);
  strncpy(settings.password, current.get_password().c_str(), sizeof(settings.password) - 1);

  if (!fixed_wifi_settings_valid_(settings)) {
    return;
  }

  if (this->fixed_wifi_have_saved_ && wifi_value_equals_(this->last_fixed_wifi_saved_.ssid, settings.ssid) &&
      wifi_value_equals_(this->last_fixed_wifi_saved_.password, settings.password)) {
    return;
  }

  if (this->save_fixed_wifi_settings_(settings)) {
    this->fixed_wifi_have_saved_ = true;
    this->last_fixed_wifi_saved_ = settings;
    ESP_LOGI(TAG, "Fixed Wi-Fi credentials updated for SSID '%s'", settings.ssid);
  } else {
    ESP_LOGW(TAG, "Failed to update fixed Wi-Fi credentials");
  }
}

bool ChangHongIceMakerPanelDebug::load_fixed_wifi_settings_(wifi::SavedWifiSettings *settings) {
  if (!this->fixed_wifi_pref_ready_ || settings == nullptr) {
    return false;
  }

  wifi::SavedWifiSettings loaded{};
  if (!this->fixed_wifi_pref_.load(&loaded) || !fixed_wifi_settings_valid_(loaded)) {
    return false;
  }

  *settings = loaded;
  return true;
}

bool ChangHongIceMakerPanelDebug::save_fixed_wifi_settings_(const wifi::SavedWifiSettings &settings) {
  if (!this->fixed_wifi_pref_ready_ || !fixed_wifi_settings_valid_(settings)) {
    return false;
  }

  const bool saved = this->fixed_wifi_pref_.save(&settings);
  if (saved && global_preferences != nullptr) {
    global_preferences->sync();
  }
  return saved;
}

bool ChangHongIceMakerPanelDebug::fixed_wifi_settings_valid_(const wifi::SavedWifiSettings &settings) {
  if (settings.ssid[0] == '\0') {
    return false;
  }
  if (wifi_value_equals_(settings.ssid, "CHANGE_ME_WIFI_SSID")) {
    return false;
  }
  if (wifi_value_equals_(settings.password, "CHANGE_ME_WIFI_PASSWORD")) {
    return false;
  }
  return true;
}

bool ChangHongIceMakerPanelDebug::wifi_value_equals_(const char *lhs, const char *rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return lhs == rhs;
  }
  return std::strcmp(lhs, rhs) == 0;
}

float ChangHongIceMakerPanelDebug::raw_mean(uint8_t index) const {
  return index < PIN_COUNT ? this->frame_raw_mean_[index] : 0.0f;
}

float ChangHongIceMakerPanelDebug::calibrated_mv_mean(uint8_t index) const {
  return index < PIN_COUNT ? this->frame_mv_mean_[index] : 0.0f;
}

float ChangHongIceMakerPanelDebug::raw_min(uint8_t index) const {
  return index < PIN_COUNT ? this->frame_raw_min_[index] : 0.0f;
}

float ChangHongIceMakerPanelDebug::raw_max(uint8_t index) const {
  return index < PIN_COUNT ? this->frame_raw_max_[index] : 0.0f;
}

float ChangHongIceMakerPanelDebug::raw_stddev(uint8_t index) const {
  return index < PIN_COUNT ? this->frame_raw_stddev_[index] : 0.0f;
}

float ChangHongIceMakerPanelDebug::digital_duty(uint8_t index) const {
  return index < PIN_COUNT ? this->frame_digital_duty_[index] : 0.0f;
}

float ChangHongIceMakerPanelDebug::edge_count(uint8_t index) const {
  return index < PIN_COUNT ? this->frame_edge_count_[index] : 0.0f;
}

float ChangHongIceMakerPanelDebug::pair_delta_mean(uint8_t index) const {
  return index < PAIR_COUNT ? this->frame_pair_delta_mean_[index] : 0.0f;
}

void ChangHongIceMakerPanelDebug::configure_pins_() {
  for (uint8_t pin : this->pins_) {
    if (this->bias_mode_ == BiasMode::INTERNAL_PULLUP) {
      pinMode(pin, INPUT_PULLUP);
    } else if (this->bias_mode_ == BiasMode::INTERNAL_PULLDOWN) {
      pinMode(pin, INPUT_PULLDOWN);
    } else {
      pinMode(pin, INPUT);
    }
  }
}

uint16_t ChangHongIceMakerPanelDebug::read_adc_raw_(uint8_t index) {
  if (this->adc_ready_ && this->adc_handle_ != nullptr) {
    int raw = 0;
    const esp_err_t err = adc_oneshot_read(this->adc_handle_, this->adc_channels_[index], &raw);
    if (err == ESP_OK) {
      return static_cast<uint16_t>(std::clamp(raw, 0, 4095));
    }
    this->accum_.adc_read_errors++;
    return this->last_raw_[index];
  }

  return static_cast<uint16_t>(analogRead(this->pins_[index]));
}

void ChangHongIceMakerPanelDebug::drain_continuous_adc_() {
  for (uint8_t read_index = 0; read_index < MAX_CONTINUOUS_READS_PER_LOOP; read_index++) {
    uint32_t out_length = 0;
    esp_err_t err = adc_continuous_read(this->adc_continuous_handle_, this->continuous_read_buffer_.data(),
                                        this->continuous_read_buffer_.size(), &out_length, 0);
    if (err == ESP_ERR_TIMEOUT) {
      return;
    }
    if (err != ESP_OK) {
      this->accum_.adc_read_errors++;
      return;
    }

    uint32_t parsed_samples = 0;
    err = adc_continuous_parse_data(this->adc_continuous_handle_, this->continuous_read_buffer_.data(), out_length,
                                    this->continuous_parsed_data_.data(), &parsed_samples);
    if (err != ESP_OK) {
      this->accum_.adc_read_errors++;
      continue;
    }

    for (uint32_t i = 0; i < parsed_samples; i++) {
      const auto &sample = this->continuous_parsed_data_[i];
      if (!sample.valid || sample.unit != ADC_UNIT_1) {
        this->accum_.adc_read_errors++;
        continue;
      }

      bool matched = false;
      for (uint8_t pin_index = 0; pin_index < PIN_COUNT; pin_index++) {
        if (sample.channel == this->adc_channels_[pin_index]) {
          this->last_raw_[pin_index] = static_cast<uint16_t>(std::clamp<uint32_t>(sample.raw_data, 0, 4095));
          this->continuous_seen_mask_ |= 1U << pin_index;
          matched = true;
          break;
        }
      }

      if (!matched) {
        continue;
      }
      if (this->continuous_seen_mask_ == ((1U << PIN_COUNT) - 1U)) {
        this->accumulate_current_sample_();
        this->continuous_seen_mask_ = 0;
      }
    }
  }
}

void ChangHongIceMakerPanelDebug::accumulate_current_sample_() {
  char signature[6]{};
  uint8_t mask = 0;

  for (uint8_t i = 0; i < PIN_COUNT; i++) {
    const uint16_t raw = this->last_raw_[i];
    const bool digital = digitalRead(this->pins_[i]) == HIGH;
    signature[i] = this->bucket_(raw);

    if (digital) {
      mask |= 1U << i;
      this->accum_.digital_high[i]++;
    }
    if (this->accum_.have_last_digital && digital != this->accum_.last_digital[i]) {
      this->accum_.edge_count[i]++;
    }
    this->accum_.last_digital[i] = digital;

    this->accum_.raw_sum[i] += raw;
    this->accum_.raw_sq_sum[i] += static_cast<uint64_t>(raw) * raw;
    this->accum_.raw_min[i] = std::min<uint16_t>(this->accum_.raw_min[i], raw);
    this->accum_.raw_max[i] = std::max<uint16_t>(this->accum_.raw_max[i], raw);
  }

  signature[5] = '\0';
  std::memcpy(this->last_signature_, signature, sizeof(this->last_signature_));

  this->accum_.samples++;
  this->accum_.have_last_digital = true;
  this->accum_.digital_mask_count[mask]++;
  if (this->signature_is_0hhhh_(signature)) {
    this->accum_.sig_0hhhh++;
  }
  if (this->signature_is_mhmhh_(signature)) {
    this->accum_.sig_mhmhh++;
  }
  if (this->signature_is_active_hhhh_(signature)) {
    this->accum_.sig_active_hhhh++;
  }

  for (uint8_t i = 0; i < PAIR_COUNT; i++) {
    const auto pair = PAIRS[i];
    this->accum_.pair_delta_sum[i] += static_cast<int32_t>(this->last_raw_[pair[0]]) -
                                      static_cast<int32_t>(this->last_raw_[pair[1]]);
  }
}

void ChangHongIceMakerPanelDebug::sample_once_() {
  for (uint8_t i = 0; i < PIN_COUNT; i++) {
    this->last_raw_[i] = this->read_adc_raw_(i);
  }
  this->accumulate_current_sample_();
}

void ChangHongIceMakerPanelDebug::finalize_frame_(uint32_t now_ms) {
  const uint32_t samples = this->accum_.samples;
  if (samples == 0) {
    this->frame_sample_rate_hz_ = 0.0f;
    return;
  }

  const float duration_s = std::max<uint32_t>(1, now_ms - this->frame_started_ms_) / 1000.0f;
  this->frame_sample_rate_hz_ = samples / duration_s;
  this->frame_ratio_0hhhh_ = 100.0f * this->accum_.sig_0hhhh / samples;
  this->frame_ratio_mhmhh_ = 100.0f * this->accum_.sig_mhmhh / samples;
  this->frame_ratio_active_hhhh_ = 100.0f * this->accum_.sig_active_hhhh / samples;
  this->frame_adc_error_rate_ = 100.0f * this->accum_.adc_read_errors / (samples * PIN_COUNT);
  this->frame_mode_mask_ = this->mode_mask_index_();

  char mask_buffer[8]{};
  std::snprintf(mask_buffer, sizeof(mask_buffer), "0x%02X", this->frame_mode_mask_);
  this->digital_mask_text_ = mask_buffer;

  for (uint8_t i = 0; i < PIN_COUNT; i++) {
    const float mean = static_cast<float>(this->accum_.raw_sum[i]) / samples;
    const float mean_sq = static_cast<float>(this->accum_.raw_sq_sum[i]) / samples;
    const float variance = std::max(0.0f, mean_sq - mean * mean);
    this->frame_raw_mean_[i] = mean;
    this->frame_raw_min_[i] = this->accum_.raw_min[i];
    this->frame_raw_max_[i] = this->accum_.raw_max[i];
    this->frame_raw_stddev_[i] = std::sqrt(variance);
    this->frame_digital_duty_[i] = 100.0f * this->accum_.digital_high[i] / samples;
    this->frame_edge_count_[i] = this->accum_.edge_count[i];
  }

  for (uint8_t i = 0; i < PAIR_COUNT; i++) {
    this->frame_pair_delta_mean_[i] = static_cast<float>(this->accum_.pair_delta_sum[i]) / samples;
  }
  this->update_calibrated_means_();

  char buffer[256]{};
  std::snprintf(
      buffer, sizeof(buffer),
      "D1,%lu,%s,%lu,%s,%s,%.1f,%.1f,%.1f,%.2f,%.1f,%.1f,%.1f,%.1f,%.1f",
      static_cast<unsigned long>(now_ms), this->bias_mode_to_cstr_(this->bias_mode_),
      static_cast<unsigned long>(samples), this->last_signature_, this->digital_mask_text_.c_str(),
      this->frame_ratio_0hhhh_, this->frame_ratio_mhmhh_, this->frame_ratio_active_hhhh_, this->frame_adc_error_rate_,
      this->frame_raw_mean_[0], this->frame_raw_mean_[1], this->frame_raw_mean_[2], this->frame_raw_mean_[3],
      this->frame_raw_mean_[4]);
  this->frame_csv_ = buffer;
}

void ChangHongIceMakerPanelDebug::reset_accumulator_() {
  this->accum_ = Accumulator{};
  this->accum_.raw_min.fill(4095);
  this->accum_.raw_max.fill(0);
}

void ChangHongIceMakerPanelDebug::log_frame_(uint32_t now_ms) {
  ESP_LOGI(TAG, "%s", this->frame_csv_.c_str());
  ESP_LOGI(TAG,
           "D2,%lu,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f",
           static_cast<unsigned long>(now_ms), this->frame_raw_min_[0], this->frame_raw_min_[1],
           this->frame_raw_min_[2], this->frame_raw_min_[3], this->frame_raw_min_[4], this->frame_raw_max_[0],
           this->frame_raw_max_[1], this->frame_raw_max_[2], this->frame_raw_max_[3], this->frame_raw_max_[4],
           this->frame_raw_stddev_[0], this->frame_raw_stddev_[1], this->frame_raw_stddev_[2],
           this->frame_raw_stddev_[3], this->frame_raw_stddev_[4]);
  ESP_LOGI(TAG,
           "D3,%lu,%.1f,%.1f,%.1f,%.1f,%.1f,%.0f,%.0f,%.0f,%.0f,%.0f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f",
           static_cast<unsigned long>(now_ms), this->frame_digital_duty_[0], this->frame_digital_duty_[1],
           this->frame_digital_duty_[2], this->frame_digital_duty_[3], this->frame_digital_duty_[4],
           this->frame_edge_count_[0], this->frame_edge_count_[1], this->frame_edge_count_[2],
           this->frame_edge_count_[3], this->frame_edge_count_[4], this->frame_pair_delta_mean_[0],
           this->frame_pair_delta_mean_[1], this->frame_pair_delta_mean_[2], this->frame_pair_delta_mean_[3],
           this->frame_pair_delta_mean_[4], this->frame_pair_delta_mean_[5], this->frame_pair_delta_mean_[6]);
  ESP_LOGI(TAG, "D4,%lu,%s,%s,%.2f,%.1f,%.1f,%.1f,%.1f,%.1f", static_cast<unsigned long>(now_ms),
           this->adc_backend_text_.c_str(), this->adc_calibration_text_.c_str(), this->frame_adc_error_rate_,
           this->frame_mv_mean_[0], this->frame_mv_mean_[1], this->frame_mv_mean_[2], this->frame_mv_mean_[3],
           this->frame_mv_mean_[4]);
}

char ChangHongIceMakerPanelDebug::bucket_(uint16_t value) const {
  if (value < 100) {
    return '0';
  }
  if (value > 3995) {
    return 'H';
  }
  if (value >= 1500 && value <= 2500) {
    return 'M';
  }
  return 'x';
}

bool ChangHongIceMakerPanelDebug::signature_is_0hhhh_(const char *signature) const {
  return signature[0] == '0' && signature[1] == 'H' && signature[2] == 'H' && signature[3] == 'H' &&
         signature[4] == 'H';
}

bool ChangHongIceMakerPanelDebug::signature_is_mhmhh_(const char *signature) const {
  return signature[0] == 'M' && signature[1] == 'H' && signature[2] == 'M' && signature[3] == 'H' &&
         signature[4] == 'H';
}

bool ChangHongIceMakerPanelDebug::signature_is_active_hhhh_(const char *signature) const {
  return signature[1] == 'H' && signature[2] == 'H' && signature[3] == 'H' && signature[4] == 'H';
}

const char *ChangHongIceMakerPanelDebug::bias_mode_to_cstr_(BiasMode mode) const {
  switch (mode) {
    case BiasMode::FLOATING:
      return "Float";
    case BiasMode::INTERNAL_PULLUP:
      return "Internal Pullup";
    case BiasMode::INTERNAL_PULLDOWN:
      return "Internal Pulldown";
    default:
      return "Unknown";
  }
}

uint8_t ChangHongIceMakerPanelDebug::mode_mask_index_() const {
  uint8_t best_mask = 0;
  uint32_t best_count = 0;
  for (uint8_t i = 0; i < this->accum_.digital_mask_count.size(); i++) {
    if (this->accum_.digital_mask_count[i] > best_count) {
      best_count = this->accum_.digital_mask_count[i];
      best_mask = i;
    }
  }
  return best_mask;
}

void ChangHongIceMakerPanelDebug::update_calibrated_means_() {
  if (!this->adc_cali_ready_ || this->adc_cali_handle_ == nullptr) {
    this->frame_mv_mean_.fill(0.0f);
    return;
  }

  for (uint8_t i = 0; i < PIN_COUNT; i++) {
    int mv = 0;
    const int raw = static_cast<int>(std::round(this->frame_raw_mean_[i]));
    const esp_err_t err = adc_cali_raw_to_voltage(this->adc_cali_handle_, raw, &mv);
    this->frame_mv_mean_[i] = err == ESP_OK ? static_cast<float>(mv) : 0.0f;
  }
}

}  // namespace chang_hong_ice_maker_panel_debug
}  // namespace esphome
