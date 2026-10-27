#include "ClimateOptions.h"
#include <cassert>
int main() {
    assert(hvac_mode_label("off")=="Wyłączony" && hvac_mode_label("heat")=="Grzanie");
    assert(hvac_mode_label("cool")=="Chłodzenie" && hvac_mode_label("heat_cool")=="Grzanie/chłodzenie");
    assert(preset_label("none")=="Brak" && preset_label("frost")=="Antyzamarzanie");
    assert(preset_label("comfort")=="Komfort");
    // Integration-specific names stay raw.
    assert(hvac_mode_label("custom_mode")=="custom_mode" && preset_label("custom_profile")=="custom_profile");
}
