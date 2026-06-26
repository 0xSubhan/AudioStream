#include "../include/udp_sender.h"
#include <iostream>
#include <cstring>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  // Map POSIX-style names to Winsock equivalents
  #define CLOSE_SOCKET(fd) closesocket(fd)
  #define SOCKET_ERROR_CODE WSAGetLastError()
  using socket_t = SOCKET;
  static const socket_t INVALID_SOCKET_VAL = INVALID_SOCKET;
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <arpa/inet.h>
  #include <cerrno>
  #define CLOSE_SOCKET(fd) ::close(fd)
  #define SOCKET_ERROR_CODE errno
  using socket_t = int;
  static const socket_t INVALID_SOCKET_VAL = -1;
#endif


namespace audiostream {

UdpSender::UdpSender()
    : socketFd_(static_cast<int>(INVALID_SOCKET_VAL)),
      isInitialized_(false) {
    std::memset(&destAddr_, 0, sizeof(destAddr_));
#ifdef _WIN32
    // Initialize Winsock (safe to call multiple times)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[UdpSender] WSAStartup failed." << std::endl;
    }
#endif
}


UdpSender::~UdpSender() {
    closeSocket();
#ifdef _WIN32
    WSACleanup();
#endif
}


bool UdpSender::init(const std::string& ip, int port) {
    if (isInitialized_) {
        closeSocket();
    }

    // 1. Create UDP socket
    socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET_VAL) {
        std::cerr << "[UdpSender] Failed to create socket (err=" << SOCKET_ERROR_CODE << ")" << std::endl;
        return false;
    }
    socketFd_ = static_cast<int>(sock);

    // 2. Resolve target IPv4 address
    destAddr_.sin_family = AF_INET;
    destAddr_.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, ip.c_str(), &destAddr_.sin_addr) <= 0) {
        std::cerr << "[UdpSender] Invalid IP address: " << ip << std::endl;
        closeSocket();
        return false;
    }

    // 3. Optimize socket send buffer size for low latency
    int sendBufSize = 8 * 1024; // 8 KB
#ifdef _WIN32
    if (setsockopt(static_cast<SOCKET>(socketFd_), SOL_SOCKET, SO_SNDBUF,
                   reinterpret_cast<const char*>(&sendBufSize), sizeof(sendBufSize)) != 0) {
        std::cerr << "[UdpSender] Warning: Failed to set SO_SNDBUF (err=" << SOCKET_ERROR_CODE << ")" << std::endl;
    }
#else
    if (setsockopt(socketFd_, SOL_SOCKET, SO_SNDBUF, &sendBufSize, sizeof(sendBufSize)) < 0) {
        std::cerr << "[UdpSender] Warning: Failed to set SO_SNDBUF: " << std::strerror(errno) << std::endl;
    }
#endif

    // 4. "Connect" the UDP socket to bind the kernel route
#ifdef _WIN32
    if (connect(static_cast<SOCKET>(socketFd_),
                reinterpret_cast<struct sockaddr*>(&destAddr_), sizeof(destAddr_)) != 0) {
        std::cerr << "[UdpSender] connect() failed (err=" << SOCKET_ERROR_CODE << ")" << std::endl;
        closeSocket();
        return false;
    }
#else
    if (connect(socketFd_, reinterpret_cast<struct sockaddr*>(&destAddr_), sizeof(destAddr_)) < 0) {
        std::cerr << "[UdpSender] connect() failed: " << std::strerror(errno) << std::endl;
        closeSocket();
        return false;
    }
#endif

    isInitialized_ = true;
    std::cout << "[UdpSender] Successfully initialized UDP stream to " << ip << ":" << port << std::endl;
    return true;
}

int UdpSender::send(const uint8_t* data, int len) {
    if (!isInitialized_ || socketFd_ < 0) {
        std::cerr << "[UdpSender] UdpSender is not initialized!" << std::endl;
        return -1;
    }

#ifdef _WIN32
    int bytesSent = ::send(static_cast<SOCKET>(socketFd_),
                           reinterpret_cast<const char*>(data), len, 0);
    if (bytesSent == SOCKET_ERROR) {
        int err = SOCKET_ERROR_CODE;
        if (err != WSAECONNREFUSED) {
            std::cerr << "[UdpSender] send() failed (err=" << err << ")" << std::endl;
        }
    }
#else
    int bytesSent = ::send(socketFd_, data, len, 0);
    if (bytesSent < 0) {
        if (errno != ECONNREFUSED) {
            std::cerr << "[UdpSender] send() failed: " << std::strerror(errno) << std::endl;
        }
    }
#endif
    return bytesSent;
}

void UdpSender::closeSocket() {
    if (socketFd_ >= 0) {
        CLOSE_SOCKET(socketFd_);
        socketFd_ = static_cast<int>(INVALID_SOCKET_VAL);
    }
    isInitialized_ = false;
}

} // namespace audiostream
