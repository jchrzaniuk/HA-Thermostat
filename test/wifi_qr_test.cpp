#include "WifiQr.h"
#include <cassert>
int main() {
    assert(wifi_qr_payload("ONX-A1B2","7FA2C84E901D")=="WIFI:T:WPA;S:ONX-A1B2;P:7FA2C84E901D;;");
    assert(wifi_qr_escape("a;b:c,d\\e\"f")=="a\\;b\\:c\\,d\\\\e\\\"f");
}
