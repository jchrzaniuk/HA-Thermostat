#pragma once
#include <cmath>
#include <algorithm>

// Hardware-independent rules shared by firmware and host tests.
struct ClimateModel {
  float current = NAN, target = NAN, minimum = 5, maximum = 35, step = 0.5f;
  bool available = false, singleTarget = false;
  bool adjustable() const {
    return available && singleTarget && std::isfinite(target) &&
      std::isfinite(minimum) && std::isfinite(maximum) && maximum > minimum &&
      std::isfinite(step) && step > 0;
  }
  float adjust(float value, int ticks) const {
    if (!adjustable() || !std::isfinite(value)) return target;
    float next = minimum + std::round((value - minimum) / step + ticks) * step;
    return std::max(minimum, std::min(maximum, next));
  }
  // Whole-degree steps (typical for °F) are shown without a decimal place.
  int decimals(float value) const {
    bool whole = std::isfinite(step) && step >= 1 && std::fmod(step, 1.0f) == 0 &&
                 std::fmod(value, 1.0f) == 0;
    return whole ? 0 : 1;
  }
};
