/*
 * testBroadcastNoAsyn.c
 * 
 * Program to test sending a broadcast message and reading the responses.
 * This version uses native socket calls, not asyn.
 * This program requires an NSLS electrometer to be present.
 *
 * Author: Mark Rivers
 *
 * Created December 8, 2015
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <epicsThread.h>

int main(int argc, char *argv[]) {

    char buffer[256];
    size_t nwrite, nread;
    int status;
    int32_t usock;
    uint32_t arg;
    uint32_t local_addr;
    int32_t rem_len;
    static struct sockaddr_in rem;

    local_addr = *(unsigned long*) *gethostbyname("164.54.160.255")->h_addr_list;
    usock = socket(AF_INET, SOCK_DGRAM, 0);
    printf("Called socket, usock=%d\n", usock);

    arg=1;
    status = setsockopt(usock, SOL_SOCKET, SO_BROADCAST, &arg, sizeof(arg));
    printf("Called setsockopt SO_BROADCAST, status=%d\n", status);

    rem.sin_family = AF_INET;
    rem.sin_addr.s_addr =  local_addr;    // broadcast address
    rem.sin_port = htons((uint16_t)(37747));
    rem_len = sizeof(rem);

    nwrite = sendto(usock, "i\r", 2, 0, (struct sockaddr *) &rem, sizeof(rem));
    printf("Wrote to UDP port, socket=%d, nwrite=%d\n", usock, (int)nwrite);

    epicsThreadSleep(0.1);
    nread = recvfrom(usock, buffer, sizeof(buffer), 0, (struct sockaddr *)&rem, (uint32_t*)&rem_len);
    printf("Read from UDP port, nread=%d, buffer=\n%s\n", (int)nread, buffer);
    
    system("netstat -a | grep 164.54.160.255 | grep -v ca-2");
    
    return 0;
}
    
