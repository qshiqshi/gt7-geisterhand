// The little that differs between Winsock and BSD sockets.
#pragma once
#include <cstdint>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
typedef SOCKET socket_t;
constexpr socket_t kNoSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int socket_t;
constexpr socket_t kNoSocket = -1;
#endif

void netStartup();                       // once per process (Winsock needs it)
void netClose(socket_t s);
void netSetReceiveTimeout(socket_t s, int milliseconds);
void netQuietUdp(socket_t s);            // Windows: no errors for "nobody listens there"

// IPv4 addresses of this computer in its home networks, host byte order. No loopback, no
// point-to-point links (VPN), no self-assigned 169.254 addresses.
std::vector<uint32_t> netLocalAddresses();
