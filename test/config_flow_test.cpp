#include "PanelConfig.h"
#include <cassert>
int main() {
    Config c; assert(!c.wifiConfigured() && !c.connectedSetup() && !c.ready());
    c.ssid="Home"; c.password="password";
    assert(c.wifiConfigured() && !c.connectedSetup() && !c.ready());
    c.uri=ha_websocket_uri(" http://homeassistant.local:8123/ ");
    assert(c.uri=="ws://homeassistant.local:8123/api/websocket");
    c.token="test"; assert(c.connectedSetup() && !c.ready());
    c.entity="climate.room"; assert(c.ready());
    assert(ha_websocket_uri("https://ha.example.com")=="wss://ha.example.com/api/websocket");
    assert(ha_websocket_uri(c.uri)==c.uri);
    assert(ha_websocket_uri("http://").empty());
    assert(ha_websocket_uri("ftp://example.com").empty());
    assert(ha_websocket_uri("http://user:pass@ha.local").empty());
    assert(ha_websocket_uri("http://ha.local/?key=secret").empty());
    assert(ha_websocket_uri("http://ha.local/lovelace").empty());
}
