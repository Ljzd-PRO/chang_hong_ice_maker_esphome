// Copyright (c) 2026 Ljzd-PRO <me@ljzd.link>
// Project: https://github.com/Ljzd-PRO

#pragma once

#include <array>
#include <cstddef>

namespace esphome {
namespace chang_hong_ice_maker_esphome {

template<std::size_t Capacity> class PowerTargetQueue {
 public:
  static_assert(Capacity > 0, "PowerTargetQueue capacity must be greater than zero");

  bool push(bool target_on) {
    if (this->count_ >= Capacity) {
      return false;
    }
    const std::size_t tail = (this->head_ + this->count_) % Capacity;
    this->targets_[tail] = target_on;
    this->count_++;
    return true;
  }

  bool pop(bool *target_on = nullptr) {
    if (this->empty()) {
      return false;
    }
    if (target_on != nullptr) {
      *target_on = this->targets_[this->head_];
    }
    this->head_ = (this->head_ + 1) % Capacity;
    this->count_--;
    return true;
  }

  bool front() const { return this->targets_[this->head_]; }

  bool back() const {
    const std::size_t tail = (this->head_ + this->count_ - 1) % Capacity;
    return this->targets_[tail];
  }

  bool empty() const { return this->count_ == 0; }
  bool full() const { return this->count_ == Capacity; }
  std::size_t size() const { return this->count_; }
  constexpr std::size_t capacity() const { return Capacity; }

 private:
  std::array<bool, Capacity> targets_{};
  std::size_t head_{0};
  std::size_t count_{0};
};

}  // namespace chang_hong_ice_maker_esphome
}  // namespace esphome
