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
#define MAX_CHANNEL_ID_SIZE 20
#define MAX_USER_NAME_SIZE 20
#define MAX_MESSAGE_CONTENT_SIZE 1400


#include "server-run.h"
#include "udp-clients.h"


void broadcast(int udp_server_socket, uint8_t broadcastMessage[], ssize_t bytesToSend, uint16_t timeout, uint8_t retries)
{
    for(ssize_t i = 0; i < *udp_connected_clients_num; i++)
    {
        if(udp_connected_clients[i].address_length > 0 && strcmp(udp_connected_clients[i].userName, sharedUserName) != 0 
            && strcmp(udp_connected_clients[i].channelID, sharedChannelID) == 0)
        {
            ssize_t bytes_sent = sendto(udp_server_socket, broadcastMessage, bytesToSend, 0,
                                       (struct sockaddr *)&udp_connected_clients[i].address, udp_connected_clients[i].address_length);
            if (bytes_sent == -1) 
            {
                perror("sendto failed");
            }
            else
            {
                printf("SENT %s:%d | MSG\n", inet_ntoa(udp_connected_clients[i].address.sin_addr), ntohs(udp_connected_clients[i].address.sin_port));
                fflush(stdout);
            }
            sleep_milliseconds(timeout);
            for(uint8_t i = 0; i < retries; i++)
            {
                sem_wait(semC);
                if(broadcastMessage[1] == *refMessageID_1 && broadcastMessage[2] == *refMessageID_2)
                {
                    sem_post(semC);
                    break;
                }
                else
                {
                    ssize_t bytes_sent = sendto(udp_server_socket, broadcastMessage, bytesToSend, 0,
                                               (struct sockaddr *)&udp_connected_clients[i].address, udp_connected_clients[i].address_length);
                    if (bytes_sent == -1) 
                    {
                        perror("sendto failed");
                    }
                    else
                    {
                        printf("SENT %s:%d | MSG\n", inet_ntoa(udp_connected_clients[i].address.sin_addr), ntohs(udp_connected_clients[i].address.sin_port));
                        fflush(stdout);
                    }
                }
                sem_post(semC);
                sleep_milliseconds(timeout);
            }


            uint8_t msgID[2];
            memcpy(msgID, &messageID, sizeof(uint16_t));
            *messageID = *messageID + 1;
            broadcastMessage[1] = msgID[0];
            broadcastMessage[2] = msgID[1];
        }
    }

    *failed_tcp_clients = 0;
    
    // sharedMessage already created/changed

    for(unsigned i = 0; i < *tcp_clients_num; i++)
    {
        pthread_mutex_unlock(mutexS1);
        pthread_mutex_lock(mutexS2);
    }
    for(unsigned i = 0; i < *tcp_clients_num - *failed_tcp_clients; i++)
    {
        pthread_mutex_unlock(mutexR1);
        pthread_mutex_lock(mutexR2);
    }
    *tcp_clients_num = *tcp_clients_num - *failed_tcp_clients;

}








void udp_authentize(int udp_server_socket, struct sockaddr_in client_address, socklen_t client_address_length,
                    uint8_t buffer[], ssize_t bytes_received, uint16_t timeout, uint8_t retries)
{
    
    fflush(stdout);
    char displayName[MAX_DISPLAY_NAME_SIZE];
    char userName[MAX_USER_NAME_SIZE];
    char channelID[MAX_CHANNEL_ID_SIZE];
    int spaces = 0, indexDN = 0, indexUN = 0;
    for(ssize_t i = 3; i < bytes_received; i++)
    {
        if(spaces == 0 && buffer[i] != 0x00)
        {
            userName[indexUN] = buffer[i];
            indexUN++;
        }
        if(spaces == 1 && buffer[i] != 0x00)
        {
            displayName[indexDN] = buffer[i];
            indexDN++;
        }
        if(buffer[i] == 0x00)
        {
            spaces++;
        }
    }
    userName[indexUN] = '\0';
    displayName[indexDN] = '\0';

    sem_wait(semA);
    struct ClientInfo client = getClient(client_address);
    sem_post(semA);

    if(indexUN != 0 && indexDN != 0 && spaces == 3)
    {
        printf("RECV %s:%d | AUTH\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
    }



    if(indexUN != 0 && indexDN != 0 && spaces == 3 && client.address_length == 0)
    {
        strcpy(channelID, "general");
        sem_wait(semA);

        addClient(client_address, client_address_length, displayName, userName, channelID);
        uint8_t msgID[2];
        memcpy(msgID, &messageID, sizeof(uint16_t));
        *messageID = *messageID + 1;

        //sem_post(semA);

        uint8_t reply[] = {0x01, msgID[0], msgID[1], 0x01, buffer[1], buffer[2], 0x00};
        ssize_t bytes_sent = sendto(udp_server_socket, reply, sizeof(reply), 0, (struct sockaddr *)&client_address, client_address_length);
        if (bytes_sent == -1) 
        {
            perror("sendto failed");
        }
        else
        {
            printf("SENT %s:%d | REPLY\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
            fflush(stdout);
        }
        sleep_milliseconds(timeout);
        for(uint8_t i = 0; i < retries; i++)
        {
            sem_wait(semC);
            if(reply[1] == *refMessageID_1 && reply[2] == *refMessageID_2)
            {
                sem_post(semC);
                break;
            }
            else
            {
                ssize_t bytes_sent = sendto(udp_server_socket, reply, sizeof(reply), 0, (struct sockaddr *)&client_address, client_address_length);
                if (bytes_sent == -1) 
                {
                    perror("sendto failed");
                }
                else
                {
                    printf("SENT %s:%d | REPLY\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
                    fflush(stdout);
                }
            }
            sem_post(semC);
            sleep_milliseconds(timeout);
        } 

        //sem_wait(semA);

        memcpy(msgID, &messageID, sizeof(uint16_t));
        *messageID = *messageID + 1;

        uint8_t broadcastMessage[MAX_MESSAGE_SIZE];
        ssize_t bytesToSend = 0;

        broadcastMessage[bytesToSend] = 0x04;
        bytesToSend++;
        broadcastMessage[bytesToSend] = msgID[0];
        bytesToSend++;
        broadcastMessage[bytesToSend] = msgID[1];
        bytesToSend++;

        const char *name = "Server";
        for(int i = 0; name[i] != '\0'; i++)
        {
            broadcastMessage[bytesToSend] = name[i];
            bytesToSend++;
        }
        broadcastMessage[bytesToSend] = 0x00;
        bytesToSend++;
        char msgContent[MAX_MESSAGE_CONTENT_SIZE];
        strcpy(msgContent, displayName);
        strcat(msgContent, " has joined general.");
        for(int i = 0; msgContent[i] != '\0'; i++)
        {
            broadcastMessage[bytesToSend] = msgContent[i];
            bytesToSend++;
        }
        broadcastMessage[bytesToSend] = 0x00;
        bytesToSend++;

        strcpy(sharedMessage, "MSG FROM Server IS ");
        strcat(sharedMessage, displayName);
        strcat(sharedMessage, " has joined general.\r\n");

        strcpy(sharedUserName, userName);
        strcpy(sharedChannelID, "general");
        broadcast(udp_server_socket, broadcastMessage, bytesToSend, timeout, retries);

        sem_post(semA);
    }
    else
    {
        uint8_t msgID[2];
        sem_wait(semA);
        memcpy(msgID, &messageID, sizeof(uint16_t));
        *messageID = *messageID + 1;

        uint8_t reply[] = {0x01,  msgID[0], msgID[1], 0x00, buffer[1], buffer[2], 0x00};
        ssize_t bytes_sent = sendto(udp_server_socket, reply, sizeof(reply), 0, (struct sockaddr *)&client_address, client_address_length);
        if (bytes_sent == -1) 
        {
            perror("sendto failed");
        }
        else
        {
            printf("SENT %s:%d | REPLY\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
            fflush(stdout);
        }
        sleep_milliseconds(timeout);
        for(uint8_t i = 0; i < retries; i++)
        {
            sem_wait(semC);
            if(reply[1] == *refMessageID_1 && reply[2] == *refMessageID_2)
            {
                sem_post(semC);
                break;
            }
            else
            {
                ssize_t bytes_sent = sendto(udp_server_socket, reply, sizeof(reply), 0, (struct sockaddr *)&client_address, client_address_length);
                if (bytes_sent == -1) 
                {
                    perror("sendto failed");
                }
                else
                {
                    printf("SENT %s:%d | REPLY\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
                    fflush(stdout);
                }
            }
            sem_post(semC);
            sleep_milliseconds(timeout);
        }
        sem_post(semA);

    }

}























void udp_join(int udp_server_socket, struct sockaddr_in client_address, socklen_t client_address_length,
              uint8_t buffer[], ssize_t bytes_received, uint16_t timeout, uint8_t retries)
{
    fflush(stdout);
    char displayName[MAX_DISPLAY_NAME_SIZE];
    char channelID[MAX_CHANNEL_ID_SIZE];

    int spaces = 0, indexDN = 0, indexCHID = 0;
    for(ssize_t i = 3; i < bytes_received; i++)
    {
        if(spaces == 0 && buffer[i] != 0x00)
        {
            channelID[indexCHID] = buffer[i];
            indexCHID++;
        }
        if(spaces == 1 && buffer[i] != 0x00)
        {
            displayName[indexDN] = buffer[i];
            indexDN++;
        }
        if(buffer[i] == 0x00)
        {
            spaces++;
        }
    }
    channelID[indexCHID] = '\0';
    displayName[indexDN] = '\0';

    sem_wait(semA);
    struct ClientInfo client = getClient(client_address);
    sem_post(semA);

    if(indexCHID != 0 && indexDN != 0 && spaces == 2)
    {
        printf("RECV %s:%d | JOIN\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
    }



    if(indexCHID != 0 && indexDN != 0 && spaces == 2 && client.address_length != 0)
    {
        sem_wait(semA);
        uint8_t msgID[2];
        memcpy(msgID, &messageID, sizeof(uint16_t));
        *messageID = *messageID + 1;


        uint8_t reply[] = {0x01, msgID[0], msgID[1], 0x01, buffer[1], buffer[2], 0x00};
        ssize_t bytes_sent = sendto(udp_server_socket, reply, sizeof(reply), 0, (struct sockaddr *)&client_address, client_address_length);
        if (bytes_sent == -1) 
        {
            perror("sendto failed");
        }
        else
        {
            printf("SENT %s:%d | REPLY\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
            fflush(stdout);
        }
        sleep_milliseconds(timeout);
        for(uint8_t i = 0; i < retries; i++)
        {
            sem_wait(semC);
            if(reply[1] == *refMessageID_1 && reply[2] == *refMessageID_2)
            {
                sem_post(semC);
                break;
            }
            else
            {
                ssize_t bytes_sent = sendto(udp_server_socket, reply, sizeof(reply), 0, (struct sockaddr *)&client_address, client_address_length);
                if (bytes_sent == -1) 
                {
                    perror("sendto failed");
                }
                else
                {
                    printf("SENT %s:%d | REPLY\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
                    fflush(stdout);
                }
            }
            sem_post(semC);
            sleep_milliseconds(timeout);
        }



        memcpy(msgID, &messageID, sizeof(uint16_t));
        *messageID = *messageID + 1;

        uint8_t broadcastMessage[MAX_MESSAGE_SIZE];
        ssize_t bytesToSend = 0;

        broadcastMessage[bytesToSend] = 0x04;
        bytesToSend++;
        broadcastMessage[bytesToSend] = msgID[0];
        bytesToSend++;
        broadcastMessage[bytesToSend] = msgID[1];
        bytesToSend++;

        const char *name = "Server";
        for(int i = 0; name[i] != '\0'; i++)
        {
            broadcastMessage[bytesToSend] = name[i];
            bytesToSend++;
        }
        broadcastMessage[bytesToSend] = 0x00;
        bytesToSend++;
        char msgContent[MAX_MESSAGE_CONTENT_SIZE];
        strcpy(msgContent, displayName);
        strcat(msgContent, " has left ");
        strcat(msgContent, client.channelID);
        strcat(msgContent, ".");
        for(int i = 0; msgContent[i] != '\0'; i++)
        {
            broadcastMessage[bytesToSend] = msgContent[i];
            bytesToSend++;
        }
        broadcastMessage[bytesToSend] = 0x00;
        bytesToSend++;

        strcpy(sharedMessage, "MSG FROM Server IS ");
        strcat(sharedMessage, displayName);
        strcat(sharedMessage, " has left ");
        strcat(sharedMessage, client.channelID);
        strcat(sharedMessage, ".\r\n");

        strcpy(sharedUserName, client.userName);
        strcpy(sharedChannelID, client.channelID);
        broadcast(udp_server_socket, broadcastMessage, bytesToSend, timeout, retries);

        bytesToSend = 0;
        broadcastMessage[bytesToSend] = 0x04;
        bytesToSend++;
        broadcastMessage[bytesToSend] = msgID[0];
        bytesToSend++;
        broadcastMessage[bytesToSend] = msgID[1];
        bytesToSend++;

        for(int i = 0; name[i] != '\0'; i++)
        {
            broadcastMessage[bytesToSend] = name[i];
            bytesToSend++;
        }
        broadcastMessage[bytesToSend] = 0x00;
        bytesToSend++;
        
        strcpy(msgContent, displayName);
        strcat(msgContent, " has joined ");
        strcat(msgContent, channelID);
        strcat(msgContent, ".");
        for(int i = 0; msgContent[i] != '\0'; i++)
        {
            broadcastMessage[bytesToSend] = msgContent[i];
            bytesToSend++;
        }
        broadcastMessage[bytesToSend] = 0x00;
        bytesToSend++;

        strcpy(sharedMessage, "MSG FROM Server IS ");
        strcat(sharedMessage, displayName);
        strcat(sharedMessage, " has joined ");
        strcat(sharedMessage, channelID);
        strcat(sharedMessage, ".\r\n");

        updateClient(client_address, displayName, channelID);
        client = getClient(client_address);

        strcpy(client.channelID, channelID);
        strcpy(sharedUserName, client.userName);
        strcpy(sharedChannelID, client.channelID);
        broadcast(udp_server_socket, broadcastMessage, bytesToSend, timeout, retries);

        sem_post(semA);

    }
    else
    {
        uint8_t msgID[2];
        sem_wait(semA);
        memcpy(msgID, &messageID, sizeof(uint16_t));
        *messageID = *messageID + 1;

        uint8_t reply[] = {0x01,  msgID[0], msgID[1], 0x00, buffer[1], buffer[2], 0x00};
        ssize_t bytes_sent = sendto(udp_server_socket, reply, sizeof(reply), 0, (struct sockaddr *)&client_address, client_address_length);
        if (bytes_sent == -1) 
        {
            perror("sendto failed");
        }
        else
        {
            printf("SENT %s:%d | REPLY\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
            fflush(stdout);
        }
        sleep_milliseconds(timeout);
        for(uint8_t i = 0; i < retries; i++)
        {
            sem_wait(semC);
            if(reply[1] == *refMessageID_1 && reply[2] == *refMessageID_2)
            {
                sem_post(semC);
                break;
            }
            else
            {
                ssize_t bytes_sent = sendto(udp_server_socket, reply, sizeof(reply), 0, (struct sockaddr *)&client_address, client_address_length);
                if (bytes_sent == -1) 
                {
                    perror("sendto failed");
                }
                else
                {
                    printf("SENT %s:%d | REPLY\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
                    fflush(stdout);
                }
            }
            sem_post(semC);
            sleep_milliseconds(timeout);
        }


        sem_post(semA);
    }

}














void udp_message(int udp_server_socket, struct sockaddr_in client_address, uint8_t buffer[], ssize_t bytes_received, uint16_t timeout, uint8_t retries)
{
    
    fflush(stdout);
    char displayName[MAX_DISPLAY_NAME_SIZE];
    char messageContent[MAX_MESSAGE_CONTENT_SIZE];
    int spaces = 0, indexDN = 0, indexMC = 0;
    for(ssize_t i = 3; i < bytes_received; i++)
    {
        if(spaces == 0 && buffer[i] != 0x00)
        {
            displayName[indexDN] = buffer[i];
            indexDN++;
        }
        if(spaces == 1  && buffer[i] != 0x00)
        {
            messageContent[indexMC] = buffer[i];
            indexMC++;
        }
        if(buffer[i] == 0x00)
        {
            spaces++;
        }
    }
    displayName[indexDN] = '\0';
    messageContent[indexMC] = '\0';
    

    sem_wait(semA);
    struct ClientInfo client = getClient(client_address);
    
    if(indexMC != 0 && indexDN != 0 && spaces == 2)
    {
        printf("RECV %s:%d | MSG\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
    }

    if(indexMC != 0 && indexDN != 0 && spaces == 2 && client.address_length != 0)
    {
        uint8_t broadcastMessage[MAX_MESSAGE_SIZE];
        ssize_t bytesToSend = 0;

        uint8_t msgID[2];
        memcpy(msgID, &messageID, sizeof(uint16_t));
        *messageID = *messageID + 1;

        broadcastMessage[bytesToSend] = 0x04;
        bytesToSend++;
        broadcastMessage[bytesToSend] = msgID[0];
        bytesToSend++;
        broadcastMessage[bytesToSend] = msgID[1];
        bytesToSend++;

        for(int i = 0; displayName[i] != '\0'; i++)
        {
            broadcastMessage[bytesToSend] = displayName[i];
            bytesToSend++;
        }

        broadcastMessage[bytesToSend] = 0x00;
        bytesToSend++;

        for(int i = 0; messageContent[i] != '\0'; i++)
        {
            broadcastMessage[bytesToSend] = messageContent[i];
            bytesToSend++;
        }
        broadcastMessage[bytesToSend] = 0x00;
        bytesToSend++;

        strcpy(sharedMessage, "MSG FROM ");
        strcat(sharedMessage, displayName);
        strcat(sharedMessage, " IS ");
        strcat(sharedMessage, messageContent);
        strcat(sharedMessage, "\r\n");

        updateClient(client_address, displayName, client.channelID);
        client = getClient(client_address);

        strcpy(sharedUserName, client.userName);
        strcpy(sharedChannelID, client.channelID);
        broadcast(udp_server_socket, broadcastMessage, bytesToSend, timeout, retries);

    }
    sem_post(semA);
}














void udp_error(int udp_server_socket, struct sockaddr_in client_address, socklen_t client_address_length,
              uint8_t buffer[], ssize_t bytes_received, uint16_t timeout, uint8_t retries)
{
    
    fflush(stdout);
    char messageContent[MAX_MESSAGE_CONTENT_SIZE];
    int spaces = 0, indexDN = 0, indexMC = 0;
    for(ssize_t i = 3; i < bytes_received; i++)
    {
        if(spaces == 0 && buffer[i] != 0x00)
        {
            indexDN++;
        }
        if(spaces == 1  && buffer[i] != 0x00)
        {
            messageContent[indexMC] = buffer[i];
            indexMC++;
        }
        if(buffer[i] == 0x00)
        {
            spaces++;
        }
    }
    messageContent[indexMC] = '\0';
    

    sem_wait(semA);
    struct ClientInfo client = getClient(client_address);
    
    if(indexMC != 0 && indexDN != 0 && spaces == 2)
    {
        printf("RECV %s:%d | ERR\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
    }

    if(indexMC != 0 && indexDN != 0 && spaces == 2 && client.address_length != 0)
    {
        uint8_t broadcastMessage[MAX_MESSAGE_SIZE];
        ssize_t bytesToSend = 0;
        
        uint8_t reply[3];
        uint8_t msgID[2];
        memcpy(msgID, &messageID, sizeof(uint16_t));
        *messageID = *messageID + 1;

        reply[0] = 0xFF;
        reply[1] = msgID[0];
        reply[2] = msgID[1];

        ssize_t bytes_sent = sendto(udp_server_socket, reply, sizeof(reply), 0, (struct sockaddr *)&client_address, client_address_length);
        if (bytes_sent == -1) 
        {
            perror("sendto failed");
        }
        else
        {
            printf("SENT %s:%d | BYE\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
            fflush(stdout);
        }
        sleep_milliseconds(timeout);
        for(uint8_t i = 0; i < retries; i++)
        {
            sem_wait(semC);
            if(reply[1] == *refMessageID_1 && reply[2] == *refMessageID_2)
            {
                sem_post(semC);
                break;
            }
            else
            {
                ssize_t bytes_sent = sendto(udp_server_socket, reply, sizeof(reply), 0, (struct sockaddr *)&client_address, client_address_length);
                if (bytes_sent == -1) 
                {
                    perror("sendto failed");
                }
                else
                {
                    printf("SENT %s:%d | BYE\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
                    fflush(stdout);
                }
            }
            sem_post(semC);
            sleep_milliseconds(timeout);
        }




        memcpy(msgID, &messageID, sizeof(uint16_t));
        *messageID = *messageID + 1;

        broadcastMessage[bytesToSend] = 0x04;
        bytesToSend++;
        broadcastMessage[bytesToSend] = msgID[0];
        bytesToSend++;
        broadcastMessage[bytesToSend] = msgID[1];
        bytesToSend++;

        const char *name = "Server";
        for(int i = 0; name[i] != '\0'; i++)
        {
            broadcastMessage[bytesToSend] = name[i];
            bytesToSend++;
        }

        broadcastMessage[bytesToSend] = 0x00;
        bytesToSend++;
        

        strcpy(messageContent, client.displayName);
        strcat(messageContent, " has left ");
        strcat(messageContent, client.channelID);
        strcat(messageContent, ".");
        for(int i = 0; messageContent[i] != '\0'; i++)
        {
            broadcastMessage[bytesToSend] = messageContent[i];
            bytesToSend++;
        }
        broadcastMessage[bytesToSend] = 0x00;
        bytesToSend++;

        strcpy(sharedMessage, "MSG FROM Server IS ");
        strcat(sharedMessage, messageContent);
        strcat(sharedMessage, ".\r\n");


        strcpy(sharedUserName, client.userName);
        strcpy(sharedChannelID, client.channelID);
        broadcast(udp_server_socket, broadcastMessage, bytesToSend, timeout, retries);

        removeClient(client_address);

    }
    sem_post(semA);
}








void udp_bye(int udp_server_socket, struct sockaddr_in client_address, socklen_t client_address_length,
             uint16_t timeout, uint8_t retries)

{
    printf("RECV %s:%d | BYE\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
    fflush(stdout);
    sem_wait(semA);
    struct ClientInfo client = getClient(client_address);
    
    if(client.address_length != 0)
    {
        uint8_t reply[3];
        uint8_t msgID[2];
        memcpy(msgID, &messageID, sizeof(uint16_t));
        *messageID = *messageID + 1;

        reply[0] = 0xFF;
        reply[1] = msgID[0];
        reply[2] = msgID[1];

        ssize_t bytes_sent = sendto(udp_server_socket, reply, sizeof(reply), 0, (struct sockaddr *)&client_address, client_address_length);
        if (bytes_sent == -1) 
        {
            perror("sendto failed");
        }
        else
        {
            printf("SENT %s:%d | BYE\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
            fflush(stdout);
        }
        sleep_milliseconds(timeout);
        for(uint8_t i = 0; i < retries; i++)
        {
            sem_wait(semC);
            if(reply[1] == *refMessageID_1 && reply[2] == *refMessageID_2)
            {
                sem_post(semC);
                break;
            }
            else
            {
                ssize_t bytes_sent = sendto(udp_server_socket, reply, sizeof(reply), 0, (struct sockaddr *)&client_address, client_address_length);
                if (bytes_sent == -1) 
                {
                    perror("sendto failed");
                }
                else
                {
                    printf("SENT %s:%d | BYE\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
                    fflush(stdout);
                }
            }
            sem_post(semC);
            sleep_milliseconds(timeout);
        }






        uint8_t broadcastMessage[MAX_MESSAGE_SIZE];
        ssize_t bytesToSend = 0;

        
        memcpy(msgID, &messageID, sizeof(uint16_t));
        *messageID = *messageID + 1;

        broadcastMessage[bytesToSend] = 0x04;
        bytesToSend++;
        broadcastMessage[bytesToSend] = msgID[0];
        bytesToSend++;
        broadcastMessage[bytesToSend] = msgID[1];
        bytesToSend++;

        const char *name = "Server";
        for(int i = 0; name[i] != '\0'; i++)
        {
            broadcastMessage[bytesToSend] = name[i];
            bytesToSend++;
        }

        broadcastMessage[bytesToSend] = 0x00;
        bytesToSend++;
        
        char messageContent[MAX_MESSAGE_CONTENT_SIZE];
        strcpy(messageContent, client.displayName);
        strcat(messageContent, " has left ");
        strcat(messageContent, client.channelID);
        strcat(messageContent, ".");
        for(int i = 0; messageContent[i] != '\0'; i++)
        {
            broadcastMessage[bytesToSend] = messageContent[i];
            bytesToSend++;
        }
        broadcastMessage[bytesToSend] = 0x00;
        bytesToSend++;

        strcpy(sharedMessage, "MSG FROM Server IS ");
        strcat(sharedMessage, messageContent);
        strcat(sharedMessage, ".\r\n");

        strcpy(sharedUserName, client.userName);
        strcpy(sharedChannelID, client.channelID);
        broadcast(udp_server_socket, broadcastMessage, bytesToSend, timeout, retries);

        removeClient(client_address);
    }
    sem_post(semA);
}









void udp_decode(int udp_server_socket, struct sockaddr_in client_address, socklen_t client_address_length,
                uint8_t buffer[], ssize_t bytes_received, uint16_t timeout, uint8_t retries)
{
    switch (buffer[0])
    {
    case 0x02: // AUTH
        udp_authentize(udp_server_socket, client_address, client_address_length, buffer, bytes_received, timeout, retries);
        break;

    case 0x03: // JOIN
        udp_join(udp_server_socket, client_address, client_address_length, buffer, bytes_received, timeout, retries);
        break;

    case 0x04: // MSG
        udp_message(udp_server_socket, client_address, buffer, bytes_received, timeout, retries);
        break;

    case 0xFE: // ERR
        udp_error(udp_server_socket, client_address, client_address_length, buffer, bytes_received, timeout, retries);
        break;

    case 0xFF: //BYE
        udp_bye(udp_server_socket, client_address, client_address_length, timeout, retries);
        break;

    default:
        break;
    }
}