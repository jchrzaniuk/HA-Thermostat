#pragma once

// ONX2424G013: official ESP-IDF 02_KNOB_KEY_BL_test/main/lcd_config.h.
// Hardware settings are separate from per-user Wi-Fi / Home Assistant settings.
namespace board {
constexpr int lcdClock = 5;
constexpr int lcdMosi = 1;
constexpr int lcdCs = 2;
constexpr int lcdDc = 3;
constexpr int lcdReset = 8;
constexpr int backlight = 6;
constexpr int button = 9;
constexpr int encoderA = 48;
constexpr int encoderB = 47;
constexpr int width = 240;
constexpr int height = 240;
constexpr int spiClockHz = 40 * 1000 * 1000;
constexpr bool mirrorX = true;
constexpr bool mirrorY = false;
constexpr bool swapXY = false;
// Measured on the device (2026-10-27): one detent is one quadrature edge and
// every A/B state is a rest position, so each detent is exactly one step.
constexpr int encoderEdgesPerStep = 1;
constexpr bool encoderInverted = false;
// Backlight PWM as in the manufacturer example: 25 kHz, 11-bit LEDC, 10-100 %,
// gamma 2.8; lower frequencies caused visible banding.
constexpr int backlightPwmHz = 25000;
constexpr int backlightMinPercent = 10;
constexpr float backlightGamma = 2.8f;
static_assert(encoderEdgesPerStep > 0);
}
