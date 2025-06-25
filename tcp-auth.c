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

#define MAX_MESSAGE_SIZE 4096
#define MAX_DISPLAY_NAME_SIZE 20
#define MAX_USER_NAME_SIZE 20
#define MAX_CHANNEL_ID_SIZE 20


#include "server-run.h"
#include "tcp-open.h"



void handleError(int tcp_client_socket, struct sockaddr_in client_address)
{
    const char *message = "ERR FROM server IS internal error\r\n";
    ssize_t bytes_sent = send(tcp_client_socket, message, strlen(message), 0);
    printf("SENT %s:%d | ERR\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
    fflush(stdout);
    if (bytes_sent == -1) 
    {
        perror("Error sending TCP message");
        close(tcp_client_socket);
        exit(EXIT_FAILURE);
    }

    bytes_sent = send(tcp_client_socket, "BYE\r\n", strlen("BYE\r\n"), 0);
    printf("SENT %s:%d | BYE\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
    fflush(stdout);
    if (bytes_sent == -1) 
    {
        perror("Error sending TCP message");
        close(tcp_client_socket);
        exit(EXIT_FAILURE);
    }
    close(tcp_client_socket);
    exit(EXIT_FAILURE);
}



void sendResponse(int tcp_client_socket, bool success, struct sockaddr_in client_address)
{
    const char *message;
    if(success)
    {
        message = "REPLY OK IS successfully authentized\r\n";
    }
    else
    {
        message = "REPLY NOK IS error occured when authentizating\r\n";
    }
    
    ssize_t bytes_sent = send(tcp_client_socket, message, strlen(message), 0);
    printf("SENT %s:%d | REPLY\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
    fflush(stdout);
    if (bytes_sent == -1) 
    {
        perror("Error sending TCP message");
        close(tcp_client_socket);
        exit(EXIT_FAILURE);
    }
    else if(bytes_sent == 0)
    {
        close(tcp_client_socket);
        exit(EXIT_FAILURE);
    }
}




bool authentize(char buffer[], int tcp_client_socket, struct sockaddr_in client_address)
{
    const char *pattern = "^AUTH [0-9a-zA-Z]+ AS [0-9a-zA-Z]+ USING [0-9a-zA-Z]+\\s*$";
    regex_t regex;
    if (regcomp(&regex, pattern, REG_EXTENDED) != 0) 
    {
        perror("error when compile regex");
        handleError(tcp_client_socket, client_address);
    }

    if (regexec(&regex, buffer, 0, NULL, 0) == 0) 
    {
        regfree(&regex);
        return true;
    } 
    else 
    {
        regfree(&regex);
        return false;
    }
} 



void get_display_name(char buffer[], ssize_t size, char displayName[])
{
    int spaces = 0, index = 0;
    for (ssize_t i = 0; i < size; i++)
    {
        if(buffer[i] == ' ')
        {
            spaces++;
        }
                
        if(spaces == 3 && buffer[i] != ' ')
        {
            displayName[index] = buffer[i];
            index++;
        }
        else if(spaces > 3)
        {
            break;
        }
    }              
    displayName[index] = '\0';
}


void get_user_name(char buffer[], ssize_t size, char userName[])
{
    int spaces = 0, index = 0;
    for (ssize_t i = 0; i < size; i++)
    {
        if(buffer[i] == ' ')
        {
            spaces++;
        }
                
        if(spaces == 1 && buffer[i] != ' ')
        {
            userName[index] = buffer[i];
            index++;
        }
        else if(spaces > 1)
        {
            break;
        }
    }              
    userName[index] = '\0';
}


ssize_t get_end_of_message(char buffer[], ssize_t index)
{
    for(ssize_t i = 0; i < index; i++)
    {
        if(buffer[i] == '\r' && i+1 < MAX_MESSAGE_SIZE && buffer[i+1] == '\n') { return i+1; }
    }
    return -1;
}



void tcp_auth(int tcp_client_socket, int udp_server_socket, uint16_t timeout, uint8_t retries,
              struct sockaddr_in client_address)
{
    char buffer[MAX_MESSAGE_SIZE]; 
    char mainBuffer[MAX_MESSAGE_SIZE + 1];
    char workBuffer[MAX_MESSAGE_SIZE + 1];
    ssize_t index = 0;
    ssize_t bytes_received;
    while(1)
    {
        bytes_received = recv(tcp_client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received == -1) 
        {
            perror("recv");
            close(tcp_client_socket);
            exit(EXIT_FAILURE);
        } 
        else if (bytes_received == 0) 
        {
            close(tcp_client_socket);
            exit(EXIT_FAILURE);
        } 
        else 
        {
            for(ssize_t i = 0; i < bytes_received; i++)
            {
                if(index < MAX_MESSAGE_SIZE)
                {
                    mainBuffer[index] = buffer[i];
                    index++;
                }
            }
            ssize_t end = get_end_of_message(mainBuffer, index);
            if(index >= MAX_MESSAGE_SIZE && end == -1)
            {
                index = 0;
                mainBuffer[0] = '\0';
            }
            else if(end != -1)
            {
                for(ssize_t i = 0; i <= end; i++)
                {
                    workBuffer[i] = mainBuffer[i];
                }
                workBuffer[end+1] = '\0';
                ssize_t newIndex = 0;
                for(ssize_t i = end + 1; i < index; i++)
                {
                    mainBuffer[newIndex] = mainBuffer[i];
                    newIndex++;
                }
                index = newIndex;
                
                if(authentize(workBuffer, tcp_client_socket, client_address))
                {   
                    char displayName[MAX_DISPLAY_NAME_SIZE];
                    get_display_name(workBuffer, sizeof(workBuffer), displayName);
                    char userName[MAX_USER_NAME_SIZE];
                    get_user_name(workBuffer, sizeof(workBuffer), userName);
                    char channelID[MAX_CHANNEL_ID_SIZE];
                    strcpy(channelID, "general");

                    printf("RECV %s:%d | AUTH\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
                    fflush(stdout);

                    sem_wait(semA);
                    *tcp_clients_num = *tcp_clients_num + 1;
                    sem_post(semA);
                    sendResponse(tcp_client_socket, true, client_address);
                    tcp_open(tcp_client_socket, displayName, userName, channelID, mainBuffer, workBuffer,
                             index, udp_server_socket, timeout, retries, client_address);
                }
                else
                {
                    sendResponse(tcp_client_socket, false, client_address);
                }

            }

        }
    } 
}

