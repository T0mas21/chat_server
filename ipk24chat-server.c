#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h> 
#include <errno.h> 
#include <stdint.h>

#include "server-run.h"

void print_help() 
{
    printf("Usage: ./server.c [-l IP] [-p port] [-d timeout] [-r retries] [-h]\n");
    printf("Options:\n");
    printf("  -l IP\t\tServer listening IP address (default: 0.0.0.0)\n");
    printf("  -p port\tServer listening port (default: 4567)\n");
    printf("  -d timeout\tUDP confirmation timeout (default: 250)\n");
    printf("  -r retries\tMaximum number of UDP retransmissions (default: 3)\n");
    printf("  -h\t\tPrints hint\n");
}



int main(int argc, char *argv[]) 
{
    char *ip_address = "0.0.0.0";
    uint16_t port = 4567;
    uint16_t timeout = 250;
    uint8_t retries = 3;

    int opt;
    while ((opt = getopt(argc, argv, "l:p:d:r:h")) != -1) 
    {
        switch (opt) 
        {
            case 'l':
                ip_address = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'd':
                timeout = atoi(optarg);
                break;
            case 'r':
                retries = atoi(optarg);
                break;
            case 'h':
                print_help();
                return 0;
            default:
                fprintf(stderr, "Usage: %s [-l IP] [-p port] [-d timeout] [-r retries] [-h]\n", argv[0]);
                return 1;
        }
    }


    serverRun(ip_address, port, timeout, retries);

    return 0;
}