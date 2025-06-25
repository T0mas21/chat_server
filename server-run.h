#ifndef SERVER_RUN_H
#define SERVER_RUN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <stdbool.h>
#include <signal.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <semaphore.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <regex.h>

#define MAX_CLIENTS 1000
#define MAX_USER_NAME_SIZE 20
#define MAX_DISPLAY_NAME_SIZE 20
#define MAX_CHANNEL_ID_SIZE 20

struct ClientInfo {
    struct sockaddr_in address;
    socklen_t address_length;
    char displayName[MAX_DISPLAY_NAME_SIZE];
    char userName[MAX_USER_NAME_SIZE];
    char channelID[MAX_CHANNEL_ID_SIZE];
    //uint16_t messageID;
};

extern pthread_mutex_t *mutexS1, *mutexS2, *mutexR1, *mutexR2;
extern pthread_mutexattr_t attr;
extern sem_t *semA, *semC;
extern pid_t main_pid;

extern unsigned *tcp_clients_num;
extern unsigned *failed_tcp_clients;

extern struct ClientInfo *udp_connected_clients;
extern unsigned *udp_connected_clients_num;
extern uint16_t *messageID;
extern uint8_t *refMessageID_1;
extern uint8_t *refMessageID_2;


extern char *sharedMessage;
extern char *sharedUserName;
extern char *sharedChannelID;

void handle_ctrl_c(int signum);
void sleep_milliseconds(unsigned milliseconds);
void serverRun(char *ip_address, uint16_t port, uint16_t timeout, uint8_t retries);



#endif /* SERVER_RUN_H */