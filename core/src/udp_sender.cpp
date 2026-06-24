#include "../include/udp_sender.h"
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>

namespace audiostream {

UdpSender::UdpSender()
    : socketFd_(-1),
      isInitialized_(false) {
    std::memset(&destAddr_, 0, sizeof(destAddr_));
}

UdpSender::~UdpSender() {
    closeSocket();
}

bool UdpSender::init(const std::string& ip, int port) {
    if (isInitialized_) {
        closeSocket();
    }

    // 1. Create UDP socket
    socketFd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketFd_ < 0) {
        std::cerr << "[UdpSender] Failed to create socket: " << std::strerror(errno) << std::endl;
        return false;
    }

    // 2. Resolve target IPv4 address
    destAddr_.sin_family = AF_INET;
    destAddr_.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, ip.c_str(), &destAddr_.sin_addr) <= 0) {
        std::cerr << "[UdpSender] Invalid IP address: " << ip << std::endl;
        closeSocket();
        return false;
    }

    // 3. Optimize Socket Buffer sizes for ultra-low latency.
    // By reducing the OS send buffer size, we prevent the system from queuing old audio
    // packets during WiFi hiccups. Dropping outdated packets is preferred to queuing them.
    int sendBufSize = 8 * 1024; // 8 KB (holds about 30 low-latency packets)
    if (setsockopt(socketFd_, SOL_SOCKET, SO_SNDBUF, &sendBufSize, sizeof(sendBufSize)) < 0) {
        std::cerr << "[UdpSender] Warning: Failed to set SO_SNDBUF: " << std::strerror(errno) << std::endl;
    }

    // 4. "Connect" the UDP socket.
    // Connecting a UDP socket binds the destination IP/port inside the kernel.
    // This allows using fast send() instead of sendto(), skipping route lookup on every frame.
    if (connect(socketFd_, reinterpret_cast<struct sockaddr*>(&destAddr_), sizeof(destAddr_)) < 0) {
        std::cerr << "[UdpSender] connect() failed: " << std::strerror(errno) << std::endl;
        closeSocket();
        return false;
    }

    isInitialized_ = true;
    std::cout << "[UdpSender] Successfully initialized UDP stream to " << ip << ":" << port << std::endl;
    return true;
}

int UdpSender::send(const uint8_t* data, int len) {
    if (!isInitialized_ || socketFd_ < 0) {
        std::cerr << "[UdpSender] UdpSender is not initialized!" << std::endl;
        return -1;
    }

    // Standard send() can be used because the socket is pre-connected.
    int bytesSent = ::send(socketFd_, data, len, 0);
    if (bytesSent < 0) {
        // Suppress ECONNREFUSED logging to prevent console spam when the receiver is offline.
        if (errno != ECONNREFUSED) {
            std::cerr << "[UdpSender] send() failed: " << std::strerror(errno) << std::endl;
        }
    }
    return bytesSent;
}

void UdpSender::closeSocket() {
    if (socketFd_ >= 0) {
        close(socketFd_);
        socketFd_ = -1;
    }
    isInitialized_ = false;
}

} // namespace audiostream
