#pragma once
#include <string>
struct Config {
    std::string ssid, password, uri, token, entity;
    bool wifiConfigured() const { return !ssid.empty(); }
    bool connectedSetup() const { return wifiConfigured() && !uri.empty() && !token.empty(); }
    bool ready() const { return connectedSetup() && !entity.empty(); }
};
// The UI takes the ordinary HA URL; transport details stay inside firmware.
inline std::string ha_websocket_uri(std::string address) {
    const auto begin=address.find_first_not_of(" \t\r\n");
    if(begin==std::string::npos) return {};
    address=address.substr(begin,address.find_last_not_of(" \t\r\n")-begin+1);
    if(address.rfind("http://",0)==0) address.replace(0,7,"ws://");
    else if(address.rfind("https://",0)==0) address.replace(0,8,"wss://");
    size_t scheme=address.rfind("ws://",0)==0 ? 5 : address.rfind("wss://",0)==0 ? 6 : 0;
    if(!scheme || address.size()>256 || address.find_first_of("@?# \\\t\r\n")!=std::string::npos) return {};
    while(address.size()>scheme && address.back()=='/') address.pop_back();
    size_t path=address.find('/',scheme);
    std::string host=address.substr(scheme,path==std::string::npos ? std::string::npos : path-scheme);
    if(host.empty()) return {};
    if(path==std::string::npos) return address+"/api/websocket";
    if(address.substr(path)!="/api/websocket") return {};
    return address;
}
