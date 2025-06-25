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
#include "tcp-auth.h"
#include "udp-decode.h"

#define MAX_MESSAGE_SIZE 4096

pthread_mutex_t *mutexS1, *mutexS2, *mutexR1, *mutexR2;
pthread_mutexattr_t attr;
sem_t *semA, *semC;
pid_t main_pid;

unsigned *tcp_clients_num;
unsigned *failed_tcp_clients;
unsigned *udp_connected_clients_num;
uint16_t *messageID;

uint8_t *refMessageID_1;
uint8_t *refMessageID_2;

char *sharedMessage;
char *sharedUserName;
char *sharedChannelID;

int tcp_server_socket, udp_server_socket, tcp_client_socket;

struct ClientInfo *udp_connected_clients;


void handle_ctrl_c(int signum) 
{
    if(tcp_server_socket > 0) { close(tcp_server_socket); }
    if(tcp_client_socket > 0) { close(tcp_client_socket); }
    if(udp_server_socket > 0) { close(udp_server_socket); }
    if(getpid() == main_pid)
    {
        sem_close(semA);
        sem_unlink("semA");
        sem_close(semC);
        sem_unlink("semC");

        pthread_mutex_destroy(mutexS1);
        pthread_mutex_destroy(mutexR1);
        pthread_mutex_destroy(mutexS2);
        pthread_mutex_destroy(mutexR2);
        pthread_mutexattr_destroy(&attr);
        munmap(mutexS1, sizeof(pthread_mutex_t));
        munmap(mutexR1, sizeof(pthread_mutex_t));
        munmap(mutexS2, sizeof(pthread_mutex_t));
        munmap(mutexR2, sizeof(pthread_mutex_t));

        munmap(sharedMessage, MAX_MESSAGE_SIZE);
        munmap(sharedUserName, MAX_USER_NAME_SIZE);
        munmap(sharedChannelID, MAX_CHANNEL_ID_SIZE);
        munmap(tcp_clients_num, sizeof(unsigned));
        munmap(failed_tcp_clients, sizeof(unsigned));
        munmap(udp_connected_clients, MAX_CLIENTS * sizeof(struct ClientInfo));
        munmap(udp_connected_clients_num, sizeof(unsigned));
        munmap(messageID, sizeof(uint16_t));
        munmap(refMessageID_1, sizeof(uint8_t));
        munmap(refMessageID_2, sizeof(uint8_t));

        //kill(0, SIGKILL);
    }
    exit(signum);
}


void sleep_milliseconds(unsigned milliseconds) 
{
    struct timespec req;
    req.tv_sec = milliseconds / 1000;
    req.tv_nsec = (milliseconds % 1000) * 1000000;

    nanosleep(&req, NULL);
}



void serverRun(char *ip_address, uint16_t port, uint16_t timeout, uint8_t retries)
{
    // set mutexes
    mutexS1 = mmap(NULL, sizeof(pthread_mutex_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    if (mutexS1 == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    mutexS2 = mmap(NULL, sizeof(pthread_mutex_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    if (mutexS2 == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    mutexR1 = mmap(NULL, sizeof(pthread_mutex_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    if (mutexR1 == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    mutexR2 = mmap(NULL, sizeof(pthread_mutex_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    if (mutexR2 == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(mutexS1, &attr);
    pthread_mutex_init(mutexS2, &attr);
    pthread_mutex_init(mutexR1, &attr);
    pthread_mutex_init(mutexR2, &attr);

    pthread_mutex_lock(mutexS1);
    pthread_mutex_lock(mutexR1);
    pthread_mutex_lock(mutexS2);
    pthread_mutex_lock(mutexR2);

    // set mutexes

    // set semaphore
    semA = sem_open("semA", O_CREAT | O_EXCL, 0666, 1);
    if (semA == SEM_FAILED) 
    {
        if (errno == EEXIST) 
        {
            sem_unlink("semA");
            semA = sem_open("semA", O_CREAT | O_EXCL, 0666, 1);
            if (semA == SEM_FAILED) 
            {
                perror("sem_open");
                exit(EXIT_FAILURE);
            }
        } 
        else 
        {
            perror("sem_open");
            exit(EXIT_FAILURE);
        }
    }

    semC = sem_open("semC", O_CREAT | O_EXCL, 0666, 1);
    if (semC == SEM_FAILED) 
    {
        if (errno == EEXIST) 
        {
            sem_unlink("semC");
            semC = sem_open("semC", O_CREAT | O_EXCL, 0666, 1);
            if (semC == SEM_FAILED) 
            {
                perror("sem_open");
                exit(EXIT_FAILURE);
            }
        } 
        else 
        {
            perror("sem_open");
            exit(EXIT_FAILURE);
        }
    }
    // set semaphore

    main_pid = getpid();
    signal(SIGINT, handle_ctrl_c);


    // set shared variables

    tcp_clients_num = mmap(NULL, sizeof(unsigned), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (tcp_clients_num == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    *tcp_clients_num = 0;

    failed_tcp_clients = mmap(NULL, sizeof(unsigned), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (failed_tcp_clients == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    *failed_tcp_clients = 0;

    sharedMessage = mmap(NULL, MAX_MESSAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sharedMessage == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    sharedUserName = mmap(NULL, MAX_USER_NAME_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sharedUserName == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    sharedChannelID = mmap(NULL, MAX_CHANNEL_ID_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sharedChannelID == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    udp_connected_clients = mmap(NULL, MAX_CLIENTS * sizeof(struct ClientInfo), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (udp_connected_clients == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    udp_connected_clients_num = mmap(NULL, sizeof(unsigned), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (udp_connected_clients_num == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    *udp_connected_clients_num = 0;

    messageID = mmap(NULL, sizeof(uint16_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (messageID == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    *messageID = 0;

    refMessageID_1 = mmap(NULL, sizeof(uint8_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (refMessageID_1 == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    *refMessageID_1 = 0xF;

    refMessageID_2 = mmap(NULL, sizeof(uint8_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (refMessageID_2 == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    *refMessageID_2 = 0xF;

    // set shared variables


    // set address

    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    //server_address.sin_addr.s_addr = ip_address;
    if (inet_pton(AF_INET, ip_address, &server_address.sin_addr) != 1) 
    {
        perror("Invalid IP address");
        exit(EXIT_FAILURE);
    }

    // set address

    int reuse = 1;

    // tcp set

    tcp_server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tcp_server_socket == -1) 
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(tcp_server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) 
    {
        perror("Setting SO_REUSEADDR failed");
        exit(EXIT_FAILURE);
    }

    if (bind(tcp_server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) 
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(tcp_server_socket, 1) == -1) 
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in client_address;
    socklen_t client_address_length = sizeof(client_address);

    // tcp set


    // udp set

    int udp_server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_server_socket == -1) 
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(udp_server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) 
    {
        perror("Setting SO_REUSEADDR failed");
        exit(EXIT_FAILURE);
    }
    
    if (bind(udp_server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) 
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // udp set


    pid_t pid = fork();
    if(pid == 0)
    {
        signal(SIGINT, handle_ctrl_c);

        while(1)
        {
            tcp_client_socket = accept(tcp_server_socket, (struct sockaddr *)&client_address, &client_address_length);
            if (tcp_client_socket == -1) 
            {
                perror("Accept failed");
            }
            else
            {
                pid = fork();
                if(pid == 0)
                {
                    signal(SIGINT, handle_ctrl_c);
                    tcp_auth(tcp_client_socket, udp_server_socket, timeout, retries, client_address);
                }

                //close(tcp_client_socket);
            }
        }
    }
    else
    {
        uint8_t buffer[MAX_MESSAGE_SIZE];
        struct sockaddr_in client_address;
        socklen_t client_address_length = sizeof(client_address);
        uint8_t confirm[3];
        confirm[0] = 0x00;
        while(1)
        {
            ssize_t bytes_received = recvfrom(udp_server_socket, buffer, MAX_MESSAGE_SIZE, 0, (struct sockaddr *)&client_address, &client_address_length);
            if (bytes_received == -1) 
            {
                perror("recvfrom failed");
                exit(EXIT_FAILURE);
            }


            if(buffer[0] == 0x00)
            {
                printf("RECV %s:%d | CONFIRM\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
                fflush(stdout);
                pid_t pid = fork();
                if(pid == 0)
                {
                    sem_wait(semC);
                    *refMessageID_1 = buffer[1];
                    *refMessageID_2 = buffer[2];
                    sem_post(semC);
                    exit(EXIT_SUCCESS);
                }
            }
            else
            {
                confirm[1] = buffer[1];
                confirm[2] = buffer[2];
                
                ssize_t bytes_sent = sendto(udp_server_socket, confirm, sizeof(confirm), 0, (struct sockaddr *)&client_address, client_address_length);
                if (bytes_sent == -1) 
                {
                    perror("sendto failed");
                }
                else
                {
                    printf("SENT %s:%d | CONFIRM\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
                }

                pid_t pid = fork();
                if(pid == 0)
                {
                    udp_decode(udp_server_socket, client_address, client_address_length, buffer, bytes_received, timeout, retries);
                    exit(EXIT_SUCCESS);
                }
            }
        }
    }


}








