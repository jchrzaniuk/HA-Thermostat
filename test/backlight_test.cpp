#include "Backlight.h"
#include <cassert>
int main() {
    assert(backlight_duty(100)==2047);
    assert(backlight_duty(150)==2047);
    assert(backlight_duty(0)==backlight_duty(board::backlightMinPercent));
    assert(backlight_duty(10)>0);
    assert(backlight_duty(20)<backlight_duty(50) && backlight_duty(50)<backlight_duty(100));
    assert(backlight_duty(50)==294); // 2047 * 0.5^2.8
}
