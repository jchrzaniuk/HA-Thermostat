#pragma once
#include <cstdint>
#include <cstring>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

// Match the local socket address against the AP IPv4 address (network byte order).
// The dual-stack HTTP server returns IPv4 clients as ::ffff:a.b.c.d.
inline bool is_setup_address(const sockaddr_storage &address, socklen_t length,
                             uint32_t apAddress) {
    if(address.ss_family==AF_INET && length>=sizeof(sockaddr_in)) {
        sockaddr_in ipv4{};
        memcpy(&ipv4,&address,sizeof(ipv4));
        return ipv4.sin_addr.s_addr==apAddress;
    }
    if(address.ss_family==AF_INET6 && length>=sizeof(sockaddr_in6)) {
        sockaddr_in6 ipv6{};
        memcpy(&ipv6,&address,sizeof(ipv6));
        const auto *bytes=reinterpret_cast<const uint8_t*>(&ipv6.sin6_addr);
        static const uint8_t mappedPrefix[12]={0,0,0,0,0,0,0,0,0,0,0xff,0xff};
        return memcmp(bytes,mappedPrefix,sizeof(mappedPrefix))==0 &&
               memcmp(bytes+12,&apAddress,sizeof(apAddress))==0;
    }
    return false;
}
