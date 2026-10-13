#include "net.h"

void netStartup() {
#ifdef _WIN32
    static bool done = false;
    if (!done) {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
        done = true;
    }
#endif
}

void netClose(socket_t s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

void netSetReceiveTimeout(socket_t s, int milliseconds) {
#ifdef _WIN32
    DWORD value = DWORD(milliseconds);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&value), sizeof value);
#else
    timeval value;
    value.tv_sec = milliseconds / 1000;
    value.tv_usec = (milliseconds % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof value);
#endif
}

void netQuietUdp(socket_t s) {
#ifdef _WIN32
    // Asking 254 addresses makes most of them answer "port unreachable"; Windows would turn
    // every such answer into a failed receive on this socket.
    BOOL report = FALSE;
    DWORD returned = 0;
    WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12) /* SIO_UDP_CONNRESET */, &report, sizeof report, nullptr, 0, &returned, nullptr, nullptr);
#else
    (void)s;
#endif
}

namespace {
bool homeAddress(uint32_t ip) {
    const uint32_t a = ip >> 24, b = (ip >> 16) & 0xFF;
    return a != 127 && a != 0 && !(a == 169 && b == 254);
}
}  // namespace

std::vector<uint32_t> netLocalAddresses() {
    std::vector<uint32_t> result;
#ifdef _WIN32
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buffer(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG rc = GetAdaptersAddresses(AF_INET, flags, nullptr, adapters, &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        rc = GetAdaptersAddresses(AF_INET, flags, nullptr, adapters, &size);
    }
    if (rc != NO_ERROR) return result;
    for (auto* a = adapters; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || a->IfType == IF_TYPE_PPP ||
            a->IfType == IF_TYPE_TUNNEL)
            continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
            const uint32_t ip = ntohl(reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr);
            if (homeAddress(ip)) result.push_back(ip);
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return result;
    for (ifaddrs* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        if (!(a->ifa_flags & IFF_UP) || (a->ifa_flags & (IFF_LOOPBACK | IFF_POINTOPOINT))) continue;
        const uint32_t ip = ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr);
        if (homeAddress(ip)) result.push_back(ip);
    }
    freeifaddrs(list);
#endif
    return result;
}
