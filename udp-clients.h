#ifndef UDP_CLIENTS_H
#define UDP_CLIENTS_H

#include <stdint.h>
#include <netinet/in.h>


void addClient(struct sockaddr_in client_address, socklen_t client_address_length, char displayName[], char userName[], char channelID[]);
struct ClientInfo getClient(struct sockaddr_in client_address);
void removeClient(struct sockaddr_in client_address);
void updateClient(struct sockaddr_in client_address, char newDisplayName[], char newChannelID[]);

#endif /* UDP_CLIENTS_H */