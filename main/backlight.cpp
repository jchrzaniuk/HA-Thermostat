#include "app.h"
#include "Backlight.h"
#include "driver/ledc.h"

namespace {
constexpr auto mode=LEDC_LOW_SPEED_MODE;
constexpr auto channel=LEDC_CHANNEL_0;
int current=-1;
}
void backlight_init(int percent) {
    ledc_timer_config_t timer{};
    timer.speed_mode=mode; timer.timer_num=LEDC_TIMER_0; timer.duty_resolution=LEDC_TIMER_11_BIT;
    timer.freq_hz=board::backlightPwmHz; timer.clk_cfg=LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t output{};
    output.gpio_num=board::backlight; output.speed_mode=mode; output.channel=channel;
    output.timer_sel=LEDC_TIMER_0; output.duty=backlight_duty(percent);
    ESP_ERROR_CHECK(ledc_channel_config(&output));
    ESP_ERROR_CHECK(ledc_fade_func_install(0));
    current=percent;
}
void backlight_set(int percent,int fadeMs) {
    if(percent==current) return;
    current=percent;
    ledc_set_fade_time_and_start(mode,channel,backlight_duty(percent),fadeMs,LEDC_FADE_NO_WAIT);
}
