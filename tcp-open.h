#ifndef TCP_OPEN_H
#define TCP_OPEN_H


void tcp_open(int tcp_client_socket, char displayName[], char userName[], char channelID[], char mainBuffer[], char workBuffer[],
              ssize_t index, int udp_server_socket, uint16_t timeout, uint8_t retries, struct sockaddr_in client_address);

#endif /* TCP_OPEN_H */