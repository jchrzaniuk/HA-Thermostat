#pragma once
#include <string>
#include <vector>
#include "ClimateModel.h"
#include "PanelConfig.h"
#include "QuickMenu.h"
#include "esp_err.h"

// Steady HA status; the display replaces it with the active preset.
inline constexpr const char *statusConnected="Połączono";
struct PanelState {
    ClimateModel climate;
    std::string name = "Termostat", action, mode, unit = "°C";
    std::string status = "Łączenie z HA";
    std::vector<std::string> modes, presets;
    std::string preset;
    bool authenticated = false;
    bool commandPending = false;
};
struct EntityChoice { std::string id, name; };
Config config_load();
esp_err_t config_save(const Config &config);
void network_start(const Config &config, bool setup);
void portal_start(const Config &config);
const char *setup_ssid();
const char *setup_password();
// setup: also fetch the full entity list for the configuration page.
void ha_start(const Config &config, bool setup);
void ha_poll();
PanelState ha_state();
std::vector<EntityChoice> ha_entities();
bool ha_set_temperature(float temperature);
bool ha_set_mode(const std::string &mode);
bool ha_set_preset(const std::string &preset);
void display_init();
void backlight_init(int percent);
void backlight_set(int percent, int fadeMs);
void display_update(const PanelState &state, float preview, const char *message,
                    const QuickMenu *menu = nullptr);
void display_setup(const char *ssid, const char *password);

std::string network_ip();
void network_setup_ap(bool enabled);
// Disconnects from Wi-Fi before restarting.
void panel_restart();
void display_network(const std::string &ip);
