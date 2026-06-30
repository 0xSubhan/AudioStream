#pragma once

#include <string>
#include <cstdint>

// Cross-platform socket type
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>  // provides SOCKET, struct sockaddr_in on Windows
  using socket_t = SOCKET;
#else
  #include <netinet/in.h>
  using socket_t = int;
#endif

namespace audiostream {

class UdpSender {
public:
    UdpSender();
    ~UdpSender();

    // Prevent copy/assignment to avoid duplicate socket descriptor lifetime conflicts
    UdpSender(const UdpSender&) = delete;
    UdpSender& operator=(const UdpSender&) = delete;

    /**
     * Opens a UDP socket and binds the destination destination address and port.
     * @param ip Destination IPv4 address (e.g. 127.0.0.1 or client IP).
     * @param port Destination port (e.g. 8554).
     * @return true if socket was successfully initialized and set up, false otherwise.
     */
    bool init(const std::string& ip, int port);

    /**
     * Transmits a packet of bytes over the UDP socket.
     * @param data Pointer to the buffer of data.
     * @param len Size of data in bytes.
     * @return Number of bytes transmitted, or -1 on error.
     */
    int send(const uint8_t* data, int len);

    /**
     * Closes the active UDP socket.
     */
    void closeSocket();

private:
    socket_t socketFd_;  // SOCKET (UINT_PTR) on Windows, int on Linux
    struct sockaddr_in destAddr_;
    bool isInitialized_;
};

} // namespace audiostream
