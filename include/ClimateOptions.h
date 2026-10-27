#pragma once
#include <string>
#include <vector>

// Polish names for standard HA values; unknown or integration-specific values stay raw.
inline std::string hvac_mode_label(const std::string &mode) {
    static const char *const names[][2]={
        {"off","Wyłączony"},{"heat","Grzanie"},{"cool","Chłodzenie"},
        {"heat_cool","Grzanie/chłodzenie"},{"auto","Automatyczny"},
        {"dry","Osuszanie"},{"fan_only","Wentylator"}};
    for(const auto &name:names) if(mode==name[0]) return name[1];
    return mode;
}
inline std::string preset_label(const std::string &preset) {
    static const char *const names[][2]={
        {"none","Brak"},{"eco","Eco"},{"away","Poza domem"},{"boost","Boost"},
        {"comfort","Komfort"},{"home","W domu"},{"sleep","Sen"},
        {"activity","Aktywność"},{"frost","Antyzamarzanie"}};
    for(const auto &name:names) if(preset==name[0]) return name[1];
    return preset;
}
