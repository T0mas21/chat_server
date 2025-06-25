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
#include "tcp-auth.h"


struct ThreadAttributes {
    int tcp_client_socket;
    char userName[MAX_USER_NAME_SIZE];
    char channelID[MAX_CHANNEL_ID_SIZE];
    bool failed;
    struct sockaddr_in client_address; 
};



void handleErrorOpen(int tcp_client_socket, struct ThreadAttributes *attr, struct sockaddr_in client_address)
{
    const char *message = "ERR FROM server IS internal error\r\n";
    ssize_t bytes_sent = send(tcp_client_socket, message, strlen(message), 0);
    if (bytes_sent == -1) 
    {
        perror("Error sending TCP message");
        //close(tcp_client_socket);
        //exit(EXIT_FAILURE);
    }
    else
    {
        printf("SENT %s:%d | ERR\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
        fflush(stdout);
    }

    bytes_sent = send(tcp_client_socket, "BYE\r\n", strlen("BYE\r\n"), 0);
    if (bytes_sent == -1) 
    {
        perror("Error sending TCP message");
        //close(tcp_client_socket);
        //exit(EXIT_FAILURE);
    }
    else
    {
        printf("SENT %s:%d | BYE\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
        fflush(stdout);
    }
    close(tcp_client_socket);
    attr->failed = true;
    //exit(EXIT_FAILURE);
}



void *receiving(void *args)
{
    struct ThreadAttributes *attr = (struct ThreadAttributes *)args;
    int tcp_client_socket = attr->tcp_client_socket;
    char *userName = attr->userName;
    char *channelID = attr->channelID;
    bool failed = attr->failed;
    struct sockaddr_in client_address = attr->client_address;

    ssize_t bytes_sent;
    while(1)
    {
        pthread_mutex_lock(mutexS1);
        if(strcmp(userName, sharedUserName) != 0 && strcmp(channelID, sharedChannelID) == 0)
        {
            bytes_sent = send(tcp_client_socket, sharedMessage, strlen(sharedMessage), 0);
            if(bytes_sent == -1)
            {
                perror("send");
                *failed_tcp_clients = *failed_tcp_clients + 1;
                pthread_mutex_unlock(mutexS2);
                free(attr);
                exit(EXIT_FAILURE);
            }
            else if(bytes_sent == 0)
            {
                *failed_tcp_clients = *failed_tcp_clients + 1;
                pthread_mutex_unlock(mutexS2);
                free(attr);
                exit(EXIT_FAILURE);
            }
            else
            {
                printf("SENT %s:%d | MSG\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
                fflush(stdout);
            }
        }
        else if(failed)
        {
            *failed_tcp_clients = *failed_tcp_clients + 1;
            pthread_mutex_unlock(mutexS2);
            free(attr);
            exit(EXIT_FAILURE);
        }
        
        pthread_mutex_unlock(mutexS2);

        pthread_mutex_lock(mutexR1);
        pthread_mutex_unlock(mutexR2);
    }
    free(attr);
    exit(EXIT_FAILURE);
}



void send_to_everyone(int udp_server_socket, uint8_t broadcastMessage[], ssize_t bytesToSend, uint16_t timeout, uint8_t retries)
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



void send_msg_to_client(int tcp_client_socket, const char *reply)
{
    ssize_t bytes_sent = send(tcp_client_socket, reply, strlen(reply), 0);
    if (bytes_sent == -1) 
    {
        perror("Error sending TCP message");
        close(tcp_client_socket);
        //exit(EXIT_FAILURE);
    }
    else if(bytes_sent == 0)
    {
        close(tcp_client_socket);
        //exit(EXIT_FAILURE);
    }
}


void decode(int tcp_client_socket, char buffer[], char displayName[], char userName[], char channelID[],
            struct ThreadAttributes *attr, int udp_server_socket, uint16_t timeout, uint8_t retries, 
            struct sockaddr_in client_address) 
{
    const char *authPattern = "^AUTH [0-9a-zA-Z]+ AS [0-9a-zA-Z]+ USING [0-9a-zA-Z]+\\s*$";
    const char *joinPattern = "^JOIN [0-9a-zA-Z]+ AS [0-9a-zA-Z]+\\s*$";
    const char *msgPattern = "^MSG FROM [0-9a-zA-Z]* IS [ -~]*\\s*$";
    const char *errPattern = "^ERR FROM [0-9a-zA-Z]* IS [ -~]*\\s*$";
    const char *byePattern = "^BYE\\s*$";

    regex_t regex;
    int ret;

    char msgContent[MAX_MESSAGE_CONTENT_SIZE];

    // AUTH pattern
    ret = regcomp(&regex, authPattern, REG_EXTENDED);
    if (ret != 0) 
    {
        perror("Error compiling authPattern");
        handleErrorOpen(tcp_client_socket, attr, client_address);
    } 
    else 
    {
        ret = regexec(&regex, buffer, 0, NULL, 0);
        if (ret == 0) 
        {
            printf("RECV %s:%d | AUTH\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
            fflush(stdout);
            send_msg_to_client(tcp_client_socket, "REPLY NOK IS Already authentized\r\n");
            printf("SENT %s:%d | REPLY\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
            fflush(stdout);
            regfree(&regex);
            return;
        } 
        else if (ret != REG_NOMATCH) 
        {
            perror("Error matching buffer against authPattern");
        }
        regfree(&regex);
    }




    // JOIN pattern
    ret = regcomp(&regex, joinPattern, REG_EXTENDED);
    if (ret != 0) 
    {
        perror("Error compiling joinPattern");
        handleErrorOpen(tcp_client_socket, attr, client_address);
    } 
    else 
    {
        ret = regexec(&regex, buffer, 0, NULL, 0);
        if (ret == 0) 
        {
            printf("RECV %s:%d | JOIN\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
            fflush(stdout);
            char oldChannelID[MAX_CHANNEL_ID_SIZE];
            strcpy(oldChannelID, channelID);
            int spaces = 0, indexDS = 0, indexCHID = 0;
            for (int i = 0; buffer[i] != '\0'; i++)
            {
                if(buffer[i] == ' ') { spaces++; }
                if(spaces == 3 && buffer[i] != '\r' && buffer[i] != '\n' && buffer[i] != ' ' && indexDS < MAX_DISPLAY_NAME_SIZE -1) 
                { 
                    displayName[indexDS] = buffer[i]; 
                    indexDS++;
                }
                if(spaces == 1 && buffer[i] != '\r' && buffer[i] != '\n' && buffer[i] != ' ' && indexCHID < MAX_CHANNEL_ID_SIZE -1)
                {
                    channelID[indexCHID] = buffer[i];
                    indexCHID++;
                }
            }
            displayName[indexDS] = '\0';
            channelID[indexCHID] = '\0';

            send_msg_to_client(tcp_client_socket, "REPLY OK IS Successfully joined\r\n");
            printf("SENT %s:%d | REPLY\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
            fflush(stdout);
            sem_wait(semA);
            strcpy(attr->channelID, oldChannelID);
            strcpy(sharedUserName, userName);
            strcpy(sharedChannelID, oldChannelID);

            strcpy(sharedMessage, "MSG FROM Server IS ");
            strcat(sharedMessage, displayName);
            strcat(sharedMessage, " has left ");
            strcat(sharedMessage, oldChannelID);
            strcat(sharedMessage, ".\r\n");


            // udp msg
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
            strcat(msgContent, oldChannelID);
            strcat(msgContent, ".");
            for(int i = 0; msgContent[i] != '\0'; i++)
            {
                broadcastMessage[bytesToSend] = msgContent[i];
                bytesToSend++;
            }
            broadcastMessage[bytesToSend] = 0x00;
            bytesToSend++;
            // udp msg

            send_to_everyone(udp_server_socket, broadcastMessage, bytesToSend, timeout, retries);

            strcpy(attr->channelID, channelID);
            strcpy(sharedUserName, userName);
            strcpy(sharedChannelID, channelID);

            strcpy(sharedMessage, "MSG FROM Server IS ");
            strcat(sharedMessage, displayName);
            strcat(sharedMessage, " has joined ");
            strcat(sharedMessage, channelID);
            strcat(sharedMessage, ".\r\n");


            // udp msg
            bytesToSend = 0;
            memcpy(msgID, &messageID, sizeof(uint16_t));
            *messageID = *messageID + 1;

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
            // udp msg

            send_to_everyone(udp_server_socket, broadcastMessage, bytesToSend, timeout, retries);
            sem_post(semA);

            regfree(&regex);
            return;

        } 
        else if (ret != REG_NOMATCH) 
        {
            perror("Error matching buffer against joinPattern");
        }
        regfree(&regex);
    }

    // MSG pattern
    ret = regcomp(&regex, msgPattern, REG_EXTENDED);
    if (ret != 0) 
    {
        perror("Error compiling msgPattern");
        handleErrorOpen(tcp_client_socket, attr, client_address);
    } 
    else 
    {
        ret = regexec(&regex, buffer, 0, NULL, 0);
        if (ret == 0) 
        {
            printf("RECV %s:%d | MSG\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
            fflush(stdout);
            int spaces = 0, indexDS = 0, indexMC = 0;
            for (int i = 0; buffer[i] != '\0'; i++)
            {
                if(buffer[i] == ' ') { spaces++; }
                if(spaces == 2 && buffer[i] != '\r' && buffer[i] != '\n' && buffer[i] != ' ' && indexDS < MAX_DISPLAY_NAME_SIZE -1) 
                { 
                    displayName[indexDS] = buffer[i]; 
                    indexDS++;
                }
                if(spaces >= 4 && buffer[i] != '\r' && buffer[i] != '\n' && indexMC < MAX_MESSAGE_CONTENT_SIZE -1)
                {
                    msgContent[indexMC] = buffer[i];
                    indexMC++;
                }
            }
            displayName[indexDS] = '\0';
            msgContent[indexMC] = '\0';

            sem_wait(semA);
            strcpy(attr->channelID, channelID);
            strcpy(sharedUserName, userName);
            strcpy(sharedChannelID, channelID);

            strcpy(sharedMessage, "MSG FROM ");
            strcat(sharedMessage, displayName);
            strcat(sharedMessage, " IS ");
            strcat(sharedMessage, msgContent);
            strcat(sharedMessage, "\r\n");


            // udp msg
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

            for(int i = 0; msgContent[i] != '\0'; i++)
            {
                broadcastMessage[bytesToSend] = msgContent[i];
                bytesToSend++;
            }
            broadcastMessage[bytesToSend] = 0x00;
            bytesToSend++;
            // udp msg

            send_to_everyone(udp_server_socket, broadcastMessage, bytesToSend, timeout, retries);
            sem_post(semA);

            regfree(&regex);
            return;

        } 
        else if (ret != REG_NOMATCH) 
        {
            perror("Error matching buffer against msgPattern");
        }
        regfree(&regex);
    }

    // Compile and match ERR pattern
    ret = regcomp(&regex, errPattern, REG_EXTENDED);
    if (ret != 0) 
    {
        perror("Error compiling errPattern");
        handleErrorOpen(tcp_client_socket, attr, client_address);
    } 
    else 
    {
        ret = regexec(&regex, buffer, 0, NULL, 0);
        if (ret == 0) 
        {
            printf("RECV %s:%d | ERR\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
            fflush(stdout);
            send_msg_to_client(tcp_client_socket, "BYE\r\n");
            printf("SENT %s:%d | BYE\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
            fflush(stdout);
            sem_wait(semA);
            strcpy(attr->channelID, channelID);
            strcpy(sharedUserName, userName);
            strcpy(sharedChannelID, channelID);

            strcpy(sharedMessage, "MSG FROM Server IS ");
            strcat(sharedMessage, displayName);
            strcat(sharedMessage, " has left ");
            strcat(sharedMessage, channelID);
            strcat(sharedMessage, ".\r\n");


            // udp msg
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

            const char *name = "Server";
            for(int i = 0; name[i] != '\0'; i++)
            {
                broadcastMessage[bytesToSend] = name[i];
                bytesToSend++;
            }

            broadcastMessage[bytesToSend] = 0x00;
            bytesToSend++;
            
            char messageContent[MAX_MESSAGE_CONTENT_SIZE];
            strcpy(messageContent, displayName);
            strcat(messageContent, " has left ");
            strcat(messageContent, channelID);
            strcat(messageContent, ".");
            for(int i = 0; messageContent[i] != '\0'; i++)
            {
                broadcastMessage[bytesToSend] = messageContent[i];
                bytesToSend++;
            }
            broadcastMessage[bytesToSend] = 0x00;
            bytesToSend++;
            // udp msg



            send_to_everyone(udp_server_socket, broadcastMessage, bytesToSend, timeout, retries);
            sem_post(semA);

            close(tcp_client_socket);
            regfree(&regex);
            return;
        } 
        else if (ret != REG_NOMATCH) 
        {
            perror("Error matching buffer against errPattern");
        }
        regfree(&regex);
    }

    // Compile and match BYE pattern
    ret = regcomp(&regex, byePattern, REG_EXTENDED);
    if (ret != 0) 
    {
        perror("Error compiling byePattern");
        handleErrorOpen(tcp_client_socket, attr, client_address);
    } 
    else 
    {
        ret = regexec(&regex, buffer, 0, NULL, 0);
        if (ret == 0) 
        {
            printf("RECV %s:%d | BYE\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
            fflush(stdout);
            send_msg_to_client(tcp_client_socket, "BYE\r\n");
            printf("SENT %s:%d | BYE\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
            fflush(stdout);
            sem_wait(semA);
            strcpy(attr->channelID, channelID);
            strcpy(sharedUserName, userName);
            strcpy(sharedChannelID, channelID);

            strcpy(sharedMessage, "MSG FROM Server IS ");
            strcat(sharedMessage, displayName);
            strcat(sharedMessage, " has left ");
            strcat(sharedMessage, channelID);
            strcat(sharedMessage, ".\r\n");



            // udp msg
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

            const char *name = "Server";
            for(int i = 0; name[i] != '\0'; i++)
            {
                broadcastMessage[bytesToSend] = name[i];
                bytesToSend++;
            }

            broadcastMessage[bytesToSend] = 0x00;
            bytesToSend++;
            
            char messageContent[MAX_MESSAGE_CONTENT_SIZE];
            strcpy(messageContent, displayName);
            strcat(messageContent, " has left ");
            strcat(messageContent, channelID);
            strcat(messageContent, ".");
            for(int i = 0; messageContent[i] != '\0'; i++)
            {
                broadcastMessage[bytesToSend] = messageContent[i];
                bytesToSend++;
            }
            broadcastMessage[bytesToSend] = 0x00;
            bytesToSend++;
            // udp msg


            send_to_everyone(udp_server_socket, broadcastMessage, bytesToSend, timeout, retries);
            sem_post(semA);

            close(tcp_client_socket);
            regfree(&regex);
            return;
        } 
        else if (ret != REG_NOMATCH) 
        {
            perror("Error matching buffer against byePattern");
        }
        regfree(&regex);
    }
}


void tcp_open(int tcp_client_socket, char displayName[], char userName[], char channelID[], char mainBuffer[], char workBuffer[],
              ssize_t index, int udp_server_socket, uint16_t timeout, uint8_t retries, struct sockaddr_in client_address)
{
    struct ThreadAttributes *attr = malloc(sizeof(struct ThreadAttributes));
    attr->tcp_client_socket = tcp_client_socket;
    strcpy(attr->userName, userName);
    strcpy(attr->channelID, channelID);
    attr->failed = false;
    
    memcpy(&(attr->client_address), &client_address, sizeof(struct sockaddr_in));

    pthread_t thread_id;
    int result = pthread_create(&thread_id, NULL, receiving, (void *)attr);
    if (result != 0) {
        perror("pthread_create");
        handleErrorOpen(tcp_client_socket, attr, client_address);
        exit(EXIT_FAILURE);
    }


    sem_wait(semA);
    strcpy(attr->channelID, channelID);
    strcpy(sharedUserName, userName);
    strcpy(sharedChannelID, channelID);

    strcpy(sharedMessage, "MSG FROM Server IS ");
    strcat(sharedMessage, displayName);
    strcat(sharedMessage, " has joined general.\r\n");


    // udp msg
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

    const char *name = "Server";
    for(int i = 0; name[i] != '\0'; i++)
    {
        broadcastMessage[bytesToSend] = name[i];
        bytesToSend++;
    }

    broadcastMessage[bytesToSend] = 0x00;
    bytesToSend++;
            
    char messageContent[MAX_MESSAGE_CONTENT_SIZE];
    strcpy(messageContent, displayName);
    strcat(messageContent, " has joined general.");
    for(int i = 0; messageContent[i] != '\0'; i++)
    {
        broadcastMessage[bytesToSend] = messageContent[i];
        bytesToSend++;
    }
    broadcastMessage[bytesToSend] = 0x00;
    bytesToSend++;
    // udp msg


    send_to_everyone(udp_server_socket, broadcastMessage, bytesToSend, timeout, retries);
    sem_post(semA);
    
    char buffer[MAX_MESSAGE_SIZE];     
    ssize_t bytes_received;
    while(1)
    {
        bytes_received = recv(tcp_client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received == -1) 
        {
            perror("recv");
            close(tcp_client_socket);
            attr->failed = true;
            //exit(EXIT_FAILURE);
            break;
        } 
        else if (bytes_received == 0) 
        {
            close(tcp_client_socket);
            attr->failed = true;
            //exit(EXIT_FAILURE);
            break;
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

                decode(tcp_client_socket, workBuffer, displayName, userName, channelID, attr, udp_server_socket, timeout, retries,
                       client_address);
            }
        }
    }
    pthread_join(thread_id, NULL);
}