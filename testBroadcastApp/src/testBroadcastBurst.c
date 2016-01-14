/*
 * testBroadcastBurst.c
 * 
 * Program to test sending a burst of broadcast messages in a loop.
 * Usage: testBroadcastBurst broadcastAddress broadcastPort numBroadcast numLoops delayTime
 *
 * This program was written to test problems with the CaenEls TetrAMM electrometer which was
 * not responding to ping when a burst of broadcast messages was received.
 *
 * Author: Mark Rivers
 *
 * Created January 14, 2015
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <sys/time.h>

int main(int argc, char *argv[]) 
{
    size_t nwrite=0;
    char buffer[256];
    int status;
    int32_t usock;
    uint32_t arg;
    uint32_t local_addr;
    int i, j;
    int numBroadcast;
    int numLoops;
    char *broadcastAddress;
    int port;
    struct timespec ts, tsrem;
    double delayTime;
    struct timeval tv;
    static struct sockaddr_in rem;

    if (argc != 6) {
        printf("Usage: testBroadcastBurst broadcastAddress broadcastPort numBroadcast numLoops delayTime\n");
        return -1;
    }
    broadcastAddress = argv[1];
    port = atoi(argv[2]);
    numBroadcast = atoi(argv[3]);
    numLoops = atoi(argv[4]);
    delayTime = atof(argv[5]);
    ts.tv_sec = (time_t)(int)delayTime;
    ts.tv_nsec = (delayTime - (int)delayTime)*1.e9;
    local_addr = *(unsigned long*) *gethostbyname(broadcastAddress)->h_addr_list;
    usock = socket(AF_INET, SOCK_DGRAM, 0);

    arg=1;
    status = setsockopt(usock, SOL_SOCKET, SO_BROADCAST, &arg, sizeof(arg));
    if (status != 0) {
        printf("Error calling setsockopt\n");
        return -1;
    }

    rem.sin_family = AF_INET;
    rem.sin_addr.s_addr =  local_addr;    // broadcast address
    rem.sin_port = htons((uint16_t)port);

    strcpy(buffer, "test\r");
    for (i=0; i<numLoops; i++) {
        for (j=0; j<numBroadcast; j++) {
          nwrite = sendto(usock, buffer, strlen(buffer), 0, (struct sockaddr *) &rem, sizeof(rem));
        }
        gettimeofday(&tv, NULL);
        printf("[%10.10ld.%6.6ld] loop %d, sent %d broadcasts to %s:%d, nwrite on last=%d\n", 
               (long)tv.tv_sec, (long)tv.tv_usec, i, j, broadcastAddress, port, (int)nwrite);
        nanosleep(&ts, &tsrem);
    }
    return 0;
}
    
