#include "chang_hong_ice_maker_esphome.h"

#include "esphome/core/log.h"

#include <Arduino.h>
#include "driver/gpio.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace esphome {
namespace chang_hong_ice_maker_esphome {

static const char *const TAG = "chang_hong_ice_maker_esphome";

void ChangHongIceMakerESPHome::setup() {
  this->setup_fixed_wifi_preferences_();

  this->current_bin_started_ms_ = millis();
  this->reset_bin_(this->current_bin_);
  this->valid_bins_ = 1;
  this->configure_inputs_only_();

  ESP_LOGI(TAG, "ChangHongIceMakerESPHome direct GPIO mode starting");
  ESP_LOGI(TAG, "GPIO mapping: P1=GPIO0 P2=GPIO1 P3=GPIO2 P4=GPIO3 P5=GPIO4");
  ESP_LOGI(TAG, "ADC sample interval=%u us, window=%u ms, bins=%u",
           SAMPLE_INTERVAL_US, BIN_INTERVAL_MS * BIN_COUNT, BIN_COUNT);
  ESP_LOGI(TAG, "Buckets: 0<100, H>3995, M=1500..2500, x=other");
  ESP_LOGW(TAG, "Direct floating GPIO mode is accepted-risk; keep panel pins input-only except explicit pulses");
}

void ChangHongIceMakerESPHome::dump_config() {
  ESP_LOGCONFIG(TAG, "Chang Hong Ice Maker ESPHome");
  ESP_LOGCONFIG(TAG, "  P1: GPIO%u", this->pins_[0]);
  ESP_LOGCONFIG(TAG, "  P2: GPIO%u", this->pins_[1]);
  ESP_LOGCONFIG(TAG, "  P3: GPIO%u", this->pins_[2]);
  ESP_LOGCONFIG(TAG, "  P4: GPIO%u", this->pins_[3]);
  ESP_LOGCONFIG(TAG, "  P5: GPIO%u", this->pins_[4]);
  ESP_LOGCONFIG(TAG, "  Sampling: 1 kHz ADC, 32 s rolling signature window");
}

void ChangHongIceMakerESPHome::loop() {
  const uint32_t now_ms = millis();
  const uint32_t now_us = micros();

  this->advance_bin_if_needed_();

  if (this->pulse_active_ && static_cast<int32_t>(now_ms - this->pulse_ends_ms_) >= 0) {
    this->finish_pulse_();
  }

  this->service_pending_mode_();

  if (!this->pulse_active_ && static_cast<uint32_t>(now_us - this->last_sample_us_) >= SAMPLE_INTERVAL_US) {
    this->last_sample_us_ = now_us;
    this->sample_once_();
  }

  if (static_cast<uint32_t>(now_ms - this->last_evaluate_ms_) >= EVALUATE_INTERVAL_MS) {
    this->last_evaluate_ms_ = now_ms;
    this->evaluate_();
  }

  if (static_cast<uint32_t>(now_ms - this->last_log_ms_) >= LOG_INTERVAL_MS) {
    this->last_log_ms_ = now_ms;
    this->log_summary_();
  }

  this->service_fixed_wifi_preferences_();
}

bool ChangHongIceMakerESPHome::request_power(bool target_on) {
  if (this->pulse_active_) {
    this->record_event_(std::string("refused_pulse_") + pulse_to_action_cstr_(this->pulse_kind_));
    ESP_LOGW(TAG, "Power request ignored: pulse already active (%s)", pulse_to_action_cstr_(this->pulse_kind_));
    return false;
  }

  if (this->optimistic_active_()) {
    const bool pending_on = this->power_on();
    if (pending_on == target_on) {
      this->record_event_("accepted_already_pending");
      ESP_LOGI(TAG, "Power request %s is already pending confirmation", target_on ? "ON" : "OFF");
      return true;
    }
    this->record_event_("refused_pending_confirmation");
    ESP_LOGW(TAG, "Power request %s refused: previous action is still pending confirmation",
             target_on ? "ON" : "OFF");
    return false;
  }

  const bool known = this->power_known();
  const bool current_on = this->power_on();
  if (!known) {
    this->record_event_("refused_unknown_state");
    ESP_LOGW(TAG, "Power request %s refused: current state is unknown", target_on ? "ON" : "OFF");
    return false;
  }
  if (current_on == target_on) {
    this->record_event_("accepted_already_satisfied");
    ESP_LOGI(TAG, "Power request %s is already satisfied", target_on ? "ON" : "OFF");
    return true;
  }

  if (!this->start_pulse_(PulseKind::SW1_OD, 1, 100)) {
    return false;
  }

  const uint32_t now = millis();
  this->optimistic_kind_ = OptimisticKind::POWER;
  this->optimistic_power_on_ = target_on;
  this->optimistic_state_ = target_on ? PanelState::STARTING : PanelState::STOPPING;
  this->optimistic_until_ms_ = now + 35000;
  this->exposed_state_ = this->optimistic_state_;
  ESP_LOGI(TAG, "Power optimistic state=%s until classifier confirms or times out",
           target_on ? "ON" : "OFF");
  return true;
}

bool ChangHongIceMakerESPHome::request_large_ice(bool target_large) {
  if (this->pulse_active_) {
    this->record_event_(std::string("refused_pulse_") + pulse_to_action_cstr_(this->pulse_kind_));
    ESP_LOGW(TAG, "Size request ignored: pulse already active (%s)", pulse_to_action_cstr_(this->pulse_kind_));
    return false;
  }

  if (this->optimistic_active_()) {
    if (this->size_known() && this->large_ice() == target_large) {
      this->record_event_("accepted_already_pending");
      ESP_LOGI(TAG, "Size request %s is already pending confirmation", target_large ? "LARGE" : "SMALL");
      return true;
    }
    this->record_event_("refused_pending_confirmation");
    ESP_LOGW(TAG, "Size request %s refused: previous action is still pending confirmation",
             target_large ? "LARGE" : "SMALL");
    return false;
  }

  if (!this->size_known()) {
    this->record_event_("refused_not_running");
    ESP_LOGW(TAG, "Size request %s refused: size is unknown or machine is not running",
             target_large ? "LARGE" : "SMALL");
    return false;
  }
  if (this->large_ice() == target_large) {
    this->record_event_("accepted_already_satisfied");
    ESP_LOGI(TAG, "Size request %s is already satisfied", target_large ? "LARGE" : "SMALL");
    return true;
  }

  if (!this->start_pulse_(PulseKind::SW2_OD, 2, 80)) {
    return false;
  }

  const uint32_t now = millis();
  this->optimistic_kind_ = OptimisticKind::SIZE;
  this->optimistic_large_ice_ = target_large;
  this->optimistic_mode_ = target_large ? PendingMode::LARGE : PendingMode::SMALL;
  this->optimistic_state_ = target_large ? PanelState::RUNNING_LARGE : PanelState::RUNNING_SMALL;
  this->optimistic_until_ms_ = now + 12000;
  this->exposed_state_ = this->optimistic_state_;
  ESP_LOGI(TAG, "Size optimistic state=%s until classifier confirms or times out",
           target_large ? "running_large" : "running_small");
  return true;
}

bool ChangHongIceMakerESPHome::request_mode(const std::string &target_mode) {
  if (target_mode == "Off") {
    this->pending_mode_after_power_on_ = PendingMode::NONE;
    return this->request_power(false);
  }

  const bool target_large = target_mode == "Large Ice";
  const bool target_small = target_mode == "Small Ice";
  if (!target_large && !target_small) {
    this->record_event_("refused_invalid_mode");
    ESP_LOGW(TAG, "Mode request refused: invalid target '%s'", target_mode.c_str());
    return false;
  }

  if (this->pulse_active_) {
    this->record_event_(std::string("refused_pulse_") + pulse_to_action_cstr_(this->pulse_kind_));
    ESP_LOGW(TAG, "Mode request %s ignored: pulse already active (%s)", target_mode.c_str(),
             pulse_to_action_cstr_(this->pulse_kind_));
    return false;
  }

  const PendingMode target_pending = target_large ? PendingMode::LARGE : PendingMode::SMALL;

  if (this->optimistic_active_()) {
    if (this->pending_mode_after_power_on_ != PendingMode::NONE) {
      return this->set_pending_mode_target_(target_pending);
    }

    if (this->size_known() && this->large_ice() == target_large) {
      this->record_event_("accepted_already_pending");
      ESP_LOGI(TAG, "Mode request %s is already pending confirmation", target_mode.c_str());
      return true;
    }

    this->record_event_("refused_pending_confirmation");
    ESP_LOGW(TAG, "Mode request %s refused: previous action is still pending confirmation",
             target_mode.c_str());
    return false;
  }

  if (this->size_known()) {
    this->pending_mode_after_power_on_ = PendingMode::NONE;
    return this->request_large_ice(target_large);
  }

  if (this->power_known() && !this->power_on()) {
    if (!this->start_pulse_(PulseKind::SW1_OD, 1, 100)) {
      return false;
    }

    const uint32_t now = millis();
    this->optimistic_kind_ = OptimisticKind::MODE;
    this->optimistic_until_ms_ = now + 45000;
    this->set_pending_mode_target_(target_pending);
    ESP_LOGI(TAG, "Mode optimistic state=%s; will correct size after startup if needed",
             target_mode.c_str());
    return true;
  }

  this->record_event_("refused_unknown_state");
  ESP_LOGW(TAG, "Mode request %s refused: current state is unknown", target_mode.c_str());
  return false;
}

bool ChangHongIceMakerESPHome::request_uv_toggle() {
  if (this->pulse_active_) {
    this->record_event_(std::string("refused_pulse_") + pulse_to_action_cstr_(this->pulse_kind_));
    ESP_LOGW(TAG, "UV toggle ignored: pulse already active (%s)", pulse_to_action_cstr_(this->pulse_kind_));
    return false;
  }
  ESP_LOGI(TAG, "UV toggle requested; no panel feedback is available for UV state");
  return this->start_pulse_(PulseKind::SW2_HOLD_OD, 2, 5000);
}

bool ChangHongIceMakerESPHome::power_known() const {
  if (this->exposed_state_ == PanelState::STARTING || this->exposed_state_ == PanelState::STOPPING) {
    return true;
  }
  return this->exposed_state_ == PanelState::STANDBY || is_running_(this->exposed_state_);
}

bool ChangHongIceMakerESPHome::power_on() const {
  if (this->optimistic_kind_ == OptimisticKind::POWER &&
      static_cast<int32_t>(millis() - this->optimistic_until_ms_) < 0) {
    return this->optimistic_power_on_;
  }
  if (this->exposed_state_ == PanelState::STARTING) {
    return true;
  }
  if (this->exposed_state_ == PanelState::STOPPING) {
    return false;
  }
  return is_running_(this->exposed_state_);
}

bool ChangHongIceMakerESPHome::size_known() const {
  if ((this->optimistic_kind_ == OptimisticKind::SIZE || this->optimistic_kind_ == OptimisticKind::MODE) &&
      static_cast<int32_t>(millis() - this->optimistic_until_ms_) < 0) {
    return this->optimistic_mode_ == PendingMode::SMALL || this->optimistic_mode_ == PendingMode::LARGE;
  }
  return this->exposed_state_ == PanelState::RUNNING_LARGE ||
         this->exposed_state_ == PanelState::RUNNING_SMALL;
}

bool ChangHongIceMakerESPHome::large_ice() const {
  if ((this->optimistic_kind_ == OptimisticKind::SIZE || this->optimistic_kind_ == OptimisticKind::MODE) &&
      static_cast<int32_t>(millis() - this->optimistic_until_ms_) < 0) {
    return this->optimistic_mode_ == PendingMode::LARGE;
  }
  return this->exposed_state_ == PanelState::RUNNING_LARGE;
}

bool ChangHongIceMakerESPHome::mode_known() const {
  return this->power_known() || this->size_known();
}

std::string ChangHongIceMakerESPHome::state_text() const {
  return state_to_cstr_(this->exposed_state_);
}

std::string ChangHongIceMakerESPHome::classified_state_text() const {
  return state_to_cstr_(this->classified_state_);
}

std::string ChangHongIceMakerESPHome::mode_text() const {
  if (!this->power_known()) {
    return "";
  }
  if (!this->power_on()) {
    return "Off";
  }
  if (!this->size_known()) {
    return "";
  }
  return this->large_ice() ? "Large Ice" : "Small Ice";
}

std::string ChangHongIceMakerESPHome::signature_text() const {
  return std::string(this->last_signature_);
}

std::string ChangHongIceMakerESPHome::action_state_text() const {
  if (this->pulse_active_) {
    return std::string("pulse_") + pulse_to_action_cstr_(this->pulse_kind_);
  }
  if (this->pending_mode_after_power_on_ != PendingMode::NONE) {
    return std::string("pending_start_") + pending_mode_to_cstr_(this->pending_mode_after_power_on_);
  }
  if (this->optimistic_active_()) {
    return std::string("confirming_") + state_to_cstr_(this->optimistic_state_);
  }
  if ((this->last_event_.rfind("refused_", 0) == 0 || this->last_event_.rfind("timeout_", 0) == 0) &&
      static_cast<uint32_t>(millis() - this->last_event_ms_) < RECENT_EVENT_VISIBLE_MS) {
    return this->last_event_;
  }
  return "idle";
}

std::string ChangHongIceMakerESPHome::action_result_text() const {
  return this->last_event_;
}

bool ChangHongIceMakerESPHome::action_busy() const {
  return this->pulse_active_ || this->pending_mode_after_power_on_ != PendingMode::NONE ||
         this->optimistic_active_();
}

void ChangHongIceMakerESPHome::setup_fixed_wifi_preferences_() {
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

void ChangHongIceMakerESPHome::service_fixed_wifi_preferences_() {
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

  if (this->fixed_wifi_have_saved_ &&
      wifi_value_equals_(this->last_fixed_wifi_saved_.ssid, settings.ssid) &&
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

bool ChangHongIceMakerESPHome::load_fixed_wifi_settings_(wifi::SavedWifiSettings *settings) {
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

bool ChangHongIceMakerESPHome::save_fixed_wifi_settings_(const wifi::SavedWifiSettings &settings) {
  if (!this->fixed_wifi_pref_ready_ || !fixed_wifi_settings_valid_(settings)) {
    return false;
  }

  const bool saved = this->fixed_wifi_pref_.save(&settings);
  if (saved && global_preferences != nullptr) {
    global_preferences->sync();
  }
  return saved;
}

bool ChangHongIceMakerESPHome::fixed_wifi_settings_valid_(const wifi::SavedWifiSettings &settings) {
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

bool ChangHongIceMakerESPHome::wifi_value_equals_(const char *lhs, const char *rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return lhs == rhs;
  }
  return std::strcmp(lhs, rhs) == 0;
}

void ChangHongIceMakerESPHome::configure_inputs_only_() {
  for (uint8_t pin : this->pins_) {
    gpio_reset_pin(static_cast<gpio_num_t>(pin));
  }
  this->restore_panel_inputs_();
}

void ChangHongIceMakerESPHome::restore_panel_inputs_() {
  for (uint8_t pin : this->pins_) {
    const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
    gpio_set_level(gpio, 0);
    gpio_set_direction(gpio, GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio, GPIO_FLOATING);
    pinMode(pin, INPUT);
  }

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
}

bool ChangHongIceMakerESPHome::start_pulse_(PulseKind kind, uint8_t pin_index, uint32_t duration_ms) {
  if (pin_index >= PIN_COUNT || this->pulse_active_) {
    return false;
  }
  if (this->cooldown_active_()) {
    this->record_event_("refused_cooldown");
    ESP_LOGW(TAG, "%s pulse refused: command cooldown active for %d ms",
             pulse_to_action_cstr_(kind), static_cast<int32_t>(this->cooldown_until_ms_ - millis()));
    return false;
  }

  this->restore_panel_inputs_();
  const uint8_t pin = this->pins_[pin_index];
  const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
  gpio_set_level(gpio, 0);
  gpio_set_direction(gpio, GPIO_MODE_OUTPUT_OD);

  this->pulse_active_ = true;
  this->pulse_kind_ = kind;
  this->pulse_pin_index_ = pin_index;
  this->pulse_duration_ms_ = duration_ms;
  this->pulse_ends_ms_ = millis() + duration_ms;

  this->record_event_(std::string("pulse_") + pulse_to_action_cstr_(kind) + "_begin");
  ESP_LOGI(TAG, "%s begin: P%u/GPIO%u low open-drain for %u ms",
           pulse_to_action_cstr_(kind), pin_index + 1, pin, duration_ms);
  return true;
}

void ChangHongIceMakerESPHome::finish_pulse_() {
  const PulseKind finished = this->pulse_kind_;
  const uint8_t pin_index = this->pulse_pin_index_;
  const uint32_t duration_ms = this->pulse_duration_ms_;

  this->restore_panel_inputs_();
  this->pulse_active_ = false;
  this->pulse_kind_ = PulseKind::NONE;
  this->pulse_duration_ms_ = 0;
  this->cooldown_until_ms_ = millis() + (finished == PulseKind::SW2_HOLD_OD ? UV_COOLDOWN_MS : SHORT_PRESS_COOLDOWN_MS);

  this->record_event_(std::string("pulse_") + pulse_to_action_cstr_(finished) + "_end");
  ESP_LOGI(TAG, "%s end: P%u restored to floating input after %u ms; cooldown=%u ms",
           pulse_to_action_cstr_(finished), pin_index + 1, duration_ms,
           finished == PulseKind::SW2_HOLD_OD ? UV_COOLDOWN_MS : SHORT_PRESS_COOLDOWN_MS);
}

void ChangHongIceMakerESPHome::service_pending_mode_() {
  if (this->pending_mode_after_power_on_ == PendingMode::NONE || this->pulse_active_) {
    return;
  }

  const bool want_large = this->pending_mode_after_power_on_ == PendingMode::LARGE;

  if (this->classified_state_ == PanelState::UNKNOWN || this->classified_state_ == PanelState::STANDBY) {
    return;
  }

  if (!is_running_(this->classified_state_)) {
    return;
  }

  const bool currently_large = this->classified_state_ == PanelState::RUNNING_LARGE;
  if (currently_large == want_large) {
    this->record_event_(std::string("confirmed_") + state_to_cstr_(this->classified_state_));
    ESP_LOGI(TAG, "Pending mode confirmed after startup: %s", want_large ? "Large Ice" : "Small Ice");
    this->pending_mode_after_power_on_ = PendingMode::NONE;
    this->optimistic_kind_ = OptimisticKind::NONE;
    this->exposed_state_ = this->classified_state_;
    return;
  }

  ESP_LOGI(TAG, "Startup reached %s; sending select pulse to reach %s",
           currently_large ? "Large Ice" : "Small Ice", want_large ? "Large Ice" : "Small Ice");
  if (this->start_pulse_(PulseKind::SW2_OD, 2, 80)) {
    this->pending_mode_after_power_on_ = PendingMode::NONE;
    this->optimistic_kind_ = OptimisticKind::MODE;
    this->optimistic_mode_ = want_large ? PendingMode::LARGE : PendingMode::SMALL;
    this->optimistic_state_ = want_large ? PanelState::RUNNING_LARGE : PanelState::RUNNING_SMALL;
    this->optimistic_until_ms_ = millis() + 12000;
    this->exposed_state_ = this->optimistic_state_;
  }
}

void ChangHongIceMakerESPHome::sample_once_() {
  char signature[6] = {'x', 'x', 'x', 'x', 'x', '\0'};
  for (uint8_t i = 0; i < PIN_COUNT; i++) {
    const uint16_t raw = static_cast<uint16_t>(analogRead(this->pins_[i]));
    this->last_raw_[i] = raw;
    signature[i] = this->bucket_(raw);
  }
  std::copy(signature, signature + 6, this->last_signature_);

  Bin &bin = this->bins_[this->current_bin_];
  bin.valid = true;
  bin.total++;
  if (this->signature_is_0hhhh_(signature)) {
    bin.sig_0hhhh++;
  }
  if (this->signature_is_mhmhh_(signature)) {
    bin.sig_mhmhh++;
  }
  for (uint8_t i = 0; i < PIN_COUNT; i++) {
    bin.raw_sum[i] += this->last_raw_[i];
  }
}

void ChangHongIceMakerESPHome::advance_bin_if_needed_() {
  const uint32_t now = millis();
  while (static_cast<uint32_t>(now - this->current_bin_started_ms_) >= BIN_INTERVAL_MS) {
    this->current_bin_started_ms_ += BIN_INTERVAL_MS;
    this->current_bin_ = (this->current_bin_ + 1) % BIN_COUNT;
    if (!this->bins_[this->current_bin_].valid && this->valid_bins_ < BIN_COUNT) {
      this->valid_bins_++;
    }
    this->reset_bin_(this->current_bin_);
  }
}

void ChangHongIceMakerESPHome::reset_bin_(uint8_t index) {
  this->bins_[index].valid = true;
  this->bins_[index].total = 0;
  this->bins_[index].sig_0hhhh = 0;
  this->bins_[index].sig_mhmhh = 0;
  this->bins_[index].raw_sum.fill(0);
}

void ChangHongIceMakerESPHome::evaluate_() {
  uint32_t total = 0;
  uint32_t sig_large = 0;
  uint32_t sig_mhmhh = 0;

  for (const auto &bin : this->bins_) {
    if (!bin.valid || bin.total == 0) {
      continue;
    }
    total += bin.total;
    sig_large += bin.sig_0hhhh;
    sig_mhmhh += bin.sig_mhmhh;
  }

  if (total == 0) {
    this->ratio_large_signature_ = 0.0f;
    this->ratio_mhmhh_signature_ = 0.0f;
    this->blink_score_ = 0.0f;
    this->confidence_ = 0.0f;
    this->update_exposed_state_(PanelState::UNKNOWN, millis());
    return;
  }

  this->ratio_large_signature_ = (100.0f * sig_large) / total;
  this->ratio_mhmhh_signature_ = (100.0f * sig_mhmhh) / total;
  this->blink_score_ = this->calculate_blink_score_();

  PanelState classified = PanelState::UNKNOWN;
  if (this->ratio_large_signature_ > 65.0f && this->blink_score_ < 60.0f) {
    classified = PanelState::RUNNING_LARGE;
    this->confidence_ = this->ratio_large_signature_;
  } else if (this->ratio_mhmhh_signature_ > 55.0f) {
    if (this->blink_score_ >= 60.0f) {
      classified = PanelState::STANDBY;
      this->confidence_ = std::min(100.0f, (this->ratio_mhmhh_signature_ + this->blink_score_) * 0.5f);
    } else {
      classified = PanelState::RUNNING_SMALL;
      this->confidence_ = this->ratio_mhmhh_signature_;
    }
  } else {
    this->confidence_ = std::max(this->ratio_large_signature_, this->ratio_mhmhh_signature_);
  }

  this->classified_state_ = classified;
  this->update_exposed_state_(classified, millis());
}

void ChangHongIceMakerESPHome::update_exposed_state_(PanelState classified, uint32_t now_ms) {
  if (this->optimistic_kind_ == OptimisticKind::NONE) {
    this->exposed_state_ = classified;
    return;
  }

  bool confirmed = false;
  if (this->optimistic_kind_ == OptimisticKind::POWER) {
    confirmed = this->optimistic_power_on_ ? is_running_(classified) : classified == PanelState::STANDBY;
  } else if (this->optimistic_kind_ == OptimisticKind::SIZE) {
    confirmed = this->optimistic_large_ice_ ? classified == PanelState::RUNNING_LARGE
                                            : classified == PanelState::RUNNING_SMALL;
  } else if (this->optimistic_kind_ == OptimisticKind::MODE) {
    if (this->optimistic_mode_ == PendingMode::SMALL) {
      confirmed = classified == PanelState::RUNNING_SMALL;
    } else if (this->optimistic_mode_ == PendingMode::LARGE) {
      confirmed = classified == PanelState::RUNNING_LARGE;
    }
  }

  if (confirmed) {
    this->record_event_(std::string("confirmed_") + state_to_cstr_(classified));
    ESP_LOGI(TAG, "Optimistic state confirmed by classifier: %s", state_to_cstr_(classified));
    this->optimistic_kind_ = OptimisticKind::NONE;
    this->optimistic_mode_ = PendingMode::NONE;
    this->exposed_state_ = classified;
    return;
  }

  if (static_cast<int32_t>(now_ms - this->optimistic_until_ms_) >= 0) {
    this->record_event_(std::string("timeout_") + state_to_cstr_(this->optimistic_state_));
    ESP_LOGW(TAG, "Optimistic state timed out; reverting to classifier: %s", state_to_cstr_(classified));
    this->optimistic_kind_ = OptimisticKind::NONE;
    this->optimistic_mode_ = PendingMode::NONE;
    this->pending_mode_after_power_on_ = PendingMode::NONE;
    this->exposed_state_ = classified;
    return;
  }

  this->exposed_state_ = this->optimistic_state_;
}

float ChangHongIceMakerESPHome::calculate_blink_score_() const {
  std::array<float, BIN_COUNT> p1_means{};
  uint8_t count = 0;

  for (uint8_t offset = 0; offset < BIN_COUNT; offset++) {
    const uint8_t index = (this->current_bin_ + 1 + offset) % BIN_COUNT;
    const Bin &bin = this->bins_[index];
    if (!bin.valid || bin.total == 0) {
      continue;
    }
    p1_means[count++] = static_cast<float>(bin.raw_sum[0]) / static_cast<float>(bin.total);
  }

  if (count < 16) {
    return 0.0f;
  }

  float min_value = p1_means[0];
  float max_value = p1_means[0];
  for (uint8_t i = 1; i < count; i++) {
    min_value = std::min(min_value, p1_means[i]);
    max_value = std::max(max_value, p1_means[i]);
  }

  const float amplitude = max_value - min_value;
  if (amplitude < 150.0f) {
    return 0.0f;
  }

  const float threshold = min_value + amplitude * 0.5f;
  uint8_t high_count = 0;
  uint8_t transitions = 0;
  bool previous_high = p1_means[0] >= threshold;
  if (previous_high) {
    high_count++;
  }
  for (uint8_t i = 1; i < count; i++) {
    const bool high = p1_means[i] >= threshold;
    if (high) {
      high_count++;
    }
    if (high != previous_high) {
      transitions++;
      previous_high = high;
    }
  }

  const float duration_s = static_cast<float>(count) * 0.5f;
  const float expected_transitions = std::max(2.0f, duration_s / 2.0f);
  const float transition_error = std::fabs(static_cast<float>(transitions) - expected_transitions) /
                                 expected_transitions;
  const float transition_score = std::max(0.0f, 1.0f - transition_error);
  const float amplitude_score = std::min(1.0f, (amplitude - 150.0f) / 100.0f);

  const float duty = static_cast<float>(high_count) / static_cast<float>(count);
  float duty_score = 0.0f;
  if (duty >= 0.12f && duty <= 0.48f) {
    duty_score = 1.0f;
  } else if (duty < 0.12f) {
    duty_score = std::max(0.0f, duty / 0.12f);
  } else {
    duty_score = std::max(0.0f, (0.70f - duty) / 0.22f);
  }

  return 100.0f * amplitude_score * transition_score * duty_score;
}

void ChangHongIceMakerESPHome::log_summary_(bool force) {
  uint32_t total = 0;
  std::array<uint64_t, PIN_COUNT> sums{};
  for (const auto &bin : this->bins_) {
    if (!bin.valid || bin.total == 0) {
      continue;
    }
    total += bin.total;
    for (uint8_t i = 0; i < PIN_COUNT; i++) {
      sums[i] += bin.raw_sum[i];
    }
  }

  if (total == 0 && !force) {
    ESP_LOGD(TAG, "summary: no ADC samples yet");
    return;
  }

  const float p1 = total == 0 ? 0.0f : static_cast<float>(sums[0]) / total;
  const float p2 = total == 0 ? 0.0f : static_cast<float>(sums[1]) / total;
  const float p3 = total == 0 ? 0.0f : static_cast<float>(sums[2]) / total;
  const float p4 = total == 0 ? 0.0f : static_cast<float>(sums[3]) / total;
  const float p5 = total == 0 ? 0.0f : static_cast<float>(sums[4]) / total;

  ESP_LOGD(TAG,
           "state=%s classifier=%s sig=%s ratio_0HHHH=%.1f ratio_MHMHH=%.1f blink=%.1f conf=%.1f "
           "mean=[%.0f,%.0f,%.0f,%.0f,%.0f]",
           state_to_cstr_(this->exposed_state_), state_to_cstr_(this->classified_state_),
           this->last_signature_, this->ratio_large_signature_, this->ratio_mhmhh_signature_,
           this->blink_score_, this->confidence_, p1, p2, p3, p4, p5);
}

char ChangHongIceMakerESPHome::bucket_(uint16_t value) const {
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

bool ChangHongIceMakerESPHome::signature_is_0hhhh_(const char *signature) const {
  return signature[0] == '0' && signature[1] == 'H' && signature[2] == 'H' &&
         signature[3] == 'H' && signature[4] == 'H';
}

bool ChangHongIceMakerESPHome::signature_is_mhmhh_(const char *signature) const {
  return signature[0] == 'M' && signature[1] == 'H' && signature[2] == 'M' &&
         signature[3] == 'H' && signature[4] == 'H';
}

bool ChangHongIceMakerESPHome::optimistic_active_() const {
  return this->optimistic_kind_ != OptimisticKind::NONE &&
         static_cast<int32_t>(millis() - this->optimistic_until_ms_) < 0;
}

bool ChangHongIceMakerESPHome::cooldown_active_() const {
  return static_cast<int32_t>(millis() - this->cooldown_until_ms_) < 0;
}

bool ChangHongIceMakerESPHome::set_pending_mode_target_(PendingMode target) {
  if (target != PendingMode::SMALL && target != PendingMode::LARGE) {
    return false;
  }

  this->pending_mode_after_power_on_ = target;
  this->optimistic_mode_ = target;
  this->optimistic_state_ = target == PendingMode::LARGE ? PanelState::RUNNING_LARGE
                                                         : PanelState::RUNNING_SMALL;
  this->exposed_state_ = this->optimistic_state_;
  this->record_event_(std::string("pending_start_") + pending_mode_to_cstr_(target));
  ESP_LOGI(TAG, "Pending startup mode target set to %s", pending_mode_to_cstr_(target));
  return true;
}

void ChangHongIceMakerESPHome::record_event_(const std::string &event) {
  this->last_event_ = event;
  this->last_event_ms_ = millis();
}

const char *ChangHongIceMakerESPHome::state_to_cstr_(PanelState state) {
  switch (state) {
    case PanelState::STANDBY:
      return "standby";
    case PanelState::RUNNING_LARGE:
      return "running_large";
    case PanelState::RUNNING_SMALL:
      return "running_small";
    case PanelState::STARTING:
      return "starting";
    case PanelState::STOPPING:
      return "stopping";
    case PanelState::UNKNOWN:
    default:
      return "unknown";
  }
}

const char *ChangHongIceMakerESPHome::pulse_to_action_cstr_(PulseKind kind) {
  switch (kind) {
    case PulseKind::SW1_OD:
      return "sw1";
    case PulseKind::SW2_OD:
      return "sw2";
    case PulseKind::SW2_HOLD_OD:
      return "uv";
    case PulseKind::NONE:
    default:
      return "none";
  }
}

const char *ChangHongIceMakerESPHome::pending_mode_to_cstr_(PendingMode mode) {
  switch (mode) {
    case PendingMode::SMALL:
      return "small";
    case PendingMode::LARGE:
      return "large";
    case PendingMode::NONE:
    default:
      return "none";
  }
}

bool ChangHongIceMakerESPHome::is_running_(PanelState state) {
  return state == PanelState::RUNNING_LARGE || state == PanelState::RUNNING_SMALL;
}

}  // namespace chang_hong_ice_maker_esphome
}  // namespace esphome
