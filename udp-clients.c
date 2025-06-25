#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <semaphore.h>
#include <netdb.h>
#include <regex.h>

#include "server-run.h"



void addClient(struct sockaddr_in client_address, socklen_t client_address_length, char displayName[], char userName[], char channelID[])
{
    ssize_t i = 0;
    while(i < *udp_connected_clients_num && udp_connected_clients[i].address_length !=0)
    {
        i++;
    }
    udp_connected_clients[i].address = client_address;
    udp_connected_clients[i].address_length = client_address_length;
    strcpy(udp_connected_clients[i].displayName, displayName);
    strcpy(udp_connected_clients[i].userName, userName);
    strcpy(udp_connected_clients[i].channelID, channelID);
    //udp_connected_clients[i].messageID = 0;
    if(i >= *udp_connected_clients_num)
    {
        *udp_connected_clients_num = *udp_connected_clients_num + 1;
    }
}


struct ClientInfo getClient(struct sockaddr_in client_address) 
{
    for (unsigned i = 0; i < *udp_connected_clients_num; i++) 
    {
        if (memcmp(&client_address, &udp_connected_clients[i].address, sizeof(struct sockaddr_in)) == 0) 
        {
            return udp_connected_clients[i];
        }
    }
    
    struct ClientInfo defaultClient;
    memset(&defaultClient, 0, sizeof(struct ClientInfo));
    defaultClient.address_length = 0;
    return defaultClient;
}


void removeClient(struct sockaddr_in client_address)
{
    for (unsigned i = 0; i < *udp_connected_clients_num; i++) 
    {
        if (memcmp(&client_address, &udp_connected_clients[i].address, sizeof(struct sockaddr_in)) == 0) 
        {
            udp_connected_clients[i].address_length = 0;
        }
    }
}


void updateClient(struct sockaddr_in client_address, char newDisplayName[], char newChannelID[])
{
    for (unsigned i = 0; i < *udp_connected_clients_num; i++)
    {
        if (memcmp(&client_address, &udp_connected_clients[i].address, sizeof(struct sockaddr_in)) == 0)
        {
            strcpy(udp_connected_clients[i].displayName ,newDisplayName);
            strcpy(udp_connected_clients[i].channelID ,newChannelID);
        }
    }
}