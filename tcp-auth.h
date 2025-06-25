#ifndef TCP_AUTH_H
#define TCP_AUTH_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>

#define MAX_MESSAGE_SIZE 4096

ssize_t get_end_of_message(char buffer[], ssize_t index);
void tcp_auth(int tcp_client_socket, int udp_server_socket, uint16_t timeout, uint8_t retries,
              struct sockaddr_in client_address);

#endif /* TCP_AUTH_H */
