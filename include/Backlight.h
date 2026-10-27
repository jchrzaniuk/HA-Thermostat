#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "BoardConfig.h"

// Perceived brightness in percent to an 11-bit LEDC duty (manufacturer curve).
inline uint32_t backlight_duty(int percent) {
    percent=std::max(board::backlightMinPercent,std::min(100,percent));
    return uint32_t(2047.0f*std::pow(percent/100.0f,board::backlightGamma)+0.5f);
}
