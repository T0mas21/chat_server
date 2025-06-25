#ifndef UDP_DECODE_H
#define UDP_DECODE_H

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

void udp_decode(int udp_server_socket, struct sockaddr_in client_address, socklen_t client_address_length,
                uint8_t buffer[], ssize_t bytes_received, uint16_t timeout, uint8_t retries);

#endif /* UDP_DECODE_H */
