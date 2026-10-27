#include "SetupAccess.h"
#include <arpa/inet.h>
#include <cassert>
int main() {
    uint32_t ap=inet_addr("192.168.4.1");
    sockaddr_storage address{};
    sockaddr_in v4{}; v4.sin_family=AF_INET; v4.sin_addr.s_addr=ap;
    memcpy(&address,&v4,sizeof(v4));
    assert(is_setup_address(address,sizeof(v4),ap));
    assert(!is_setup_address(address,sizeof(v4)-1,ap));
    v4.sin_addr.s_addr=inet_addr("192.168.1.10");
    memcpy(&address,&v4,sizeof(v4));
    assert(!is_setup_address(address,sizeof(v4),ap));
    sockaddr_in6 v6{}; v6.sin6_family=AF_INET6;
    inet_pton(AF_INET6,"::ffff:192.168.4.1",&v6.sin6_addr);
    memcpy(&address,&v6,sizeof(v6));
    assert(is_setup_address(address,sizeof(v6),ap));
    assert(!is_setup_address(address,sizeof(sockaddr_in),ap));
    inet_pton(AF_INET6,"::ffff:192.168.1.10",&v6.sin6_addr);
    memcpy(&address,&v6,sizeof(v6));
    assert(!is_setup_address(address,sizeof(v6),ap));
    inet_pton(AF_INET6,"2001:db8::c0a8:401",&v6.sin6_addr);
    memcpy(&address,&v6,sizeof(v6));
    assert(!is_setup_address(address,sizeof(v6),ap));
    address.ss_family=AF_UNSPEC;
    assert(!is_setup_address(address,sizeof(address),ap));
}
