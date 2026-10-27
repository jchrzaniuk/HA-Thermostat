#pragma once
#include <string>
inline std::string wifi_qr_escape(const std::string &value) {
    std::string escaped;
    for(char c:value) {
        if(c=='\\' || c==';' || c==',' || c==':' || c=='"') escaped+='\\';
        escaped+=c;
    }
    return escaped;
}
inline std::string wifi_qr_payload(const std::string &ssid,const std::string &password) {
    return "WIFI:T:WPA;S:"+wifi_qr_escape(ssid)+";P:"+wifi_qr_escape(password)+";;";
}
