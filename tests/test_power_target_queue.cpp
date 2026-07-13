#include "../components/chang_hong_ice_maker_esphome/power_target_queue.h"

#include <cassert>

using esphome::chang_hong_ice_maker_esphome::PowerTargetQueue;

int main() {
  PowerTargetQueue<4> queue;
  assert(queue.empty());
  assert(queue.capacity() == 4);

  assert(queue.push(true));
  assert(queue.push(false));
  assert(queue.push(true));
  assert(queue.front());
  assert(queue.back());
  assert(queue.size() == 3);

  bool target = false;
  assert(queue.pop(&target));
  assert(target);
  assert(!queue.front());

  assert(queue.push(false));
  assert(queue.push(true));
  assert(queue.full());
  assert(!queue.push(false));

  const bool expected[] = {false, true, false, true};
  for (bool value : expected) {
    assert(queue.pop(&target));
    assert(target == value);
  }
  assert(queue.empty());
  assert(!queue.pop());
  return 0;
}
