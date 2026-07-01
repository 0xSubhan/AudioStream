#include "../include/udp_sender.h"
#include <iostream>
#include <cstring>
#include <fstream>
#include <sstream>

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
  static const socket_t INVALID_SOCKET_VAL = INVALID_SOCKET;
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <arpa/inet.h>
  #include <cerrno>
  #define CLOSE_SOCKET(fd) ::close(fd)
  #define SOCKET_ERROR_CODE errno
  static const socket_t INVALID_SOCKET_VAL = -1;
#endif

inline void log_debug(const std::string& msg) {
#ifdef _WIN32
    char path[MAX_PATH];
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) != 0) {
        std::string exePath(path);
        std::string logPath = exePath.substr(0, exePath.find_last_of("\\/")) + "\\AudioStream_debug.log";
        std::ofstream file(logPath, std::ios::app);
        if (file.is_open()) {
            file << "[C++] " << msg << std::endl;
        }
    }
#else
    std::ofstream file("AudioStream_debug.log", std::ios::app);
    if (file.is_open()) {
        file << "[C++] " << msg << std::endl;
    }
#endif
}


namespace audiostream {

UdpSender::UdpSender()
    : socketFd_(INVALID_SOCKET_VAL),
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
    log_debug("UdpSender::init() called");
    if (isInitialized_) {
        log_debug("UdpSender already initialized, closing socket first");
        closeSocket();
    }

    // 1. Create UDP socket
    socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET_VAL) {
        std::ostringstream err;
        err << "Failed to create socket (err=" << SOCKET_ERROR_CODE << ")";
        log_debug(err.str());
        std::cerr << "[UdpSender] " << err.str() << std::endl;
        return false;
    }
    socketFd_ = sock;

    // 2. Resolve target IPv4 address
    destAddr_.sin_family = AF_INET;
    destAddr_.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, ip.c_str(), &destAddr_.sin_addr) <= 0) {
        std::ostringstream err;
        err << "Invalid IP address: " << ip;
        log_debug(err.str());
        std::cerr << "[UdpSender] " << err.str() << std::endl;
        closeSocket();
        return false;
    }

    // 3. Optimize socket send buffer size for low latency
    int sendBufSize = 8 * 1024; // 8 KB
#ifdef _WIN32
    if (setsockopt(static_cast<SOCKET>(socketFd_), SOL_SOCKET, SO_SNDBUF,
                   reinterpret_cast<const char*>(&sendBufSize), sizeof(sendBufSize)) != 0) {
        std::ostringstream warn;
        warn << "Warning: Failed to set SO_SNDBUF (err=" << SOCKET_ERROR_CODE << ")";
        log_debug(warn.str());
        std::cerr << "[UdpSender] " << warn.str() << std::endl;
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
        std::ostringstream err;
        err << "connect() failed (err=" << SOCKET_ERROR_CODE << ")";
        log_debug(err.str());
        std::cerr << "[UdpSender] " << err.str() << std::endl;
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
    std::ostringstream succ;
    succ << "Successfully initialized UDP stream to " << ip << ":" << port;
    log_debug(succ.str());
    std::cout << "[UdpSender] " << succ.str() << std::endl;
    return true;
}

int UdpSender::send(const uint8_t* data, int len) {
    if (!isInitialized_ || socketFd_ == INVALID_SOCKET_VAL) {
        log_debug("send() called but UdpSender is not initialized!");
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
    log_debug("UdpSender::closeSocket() called");
    if (socketFd_ != INVALID_SOCKET_VAL) {
        CLOSE_SOCKET(socketFd_);
        socketFd_ = INVALID_SOCKET_VAL;
        log_debug("Socket closed successfully");
    }
    isInitialized_ = false;
}

} // namespace audiostream
