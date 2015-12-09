/*
 * testBroadcastAsyn.c
 * 
 * Program to test sending a broadcast message and reading the responses.
 * This version uses asyn.
 * This program requires an NSLS electrometer to be present.
 *
 * Author: Mark Rivers
 *
 * Created December 8, 2015
 */

#include <stdio.h>
#include <stdlib.h>

#include <epicsThread.h>

#include <asynOctetSyncIO.h>
#include <drvAsynIPPort.h>
#include <asynShellCommands.h>

#define PORT_NAME "PORT"
#define NLOOPS 5
int main(int argc, char *argv[]) {

    char buffer[256];
    asynStatus status;
    size_t nwrite, nread;
    int eomReason;
    int i;
    asynUser *pasynUser;
    
    status = (asynStatus)drvAsynIPPortConfigure(PORT_NAME, "164.54.160.255:37747 UDP*", 0, 0, 0);
    printf("Called drvAsynIPPortConfigure for UDP port, status=%d\n", status);

    /* Set ASYN_TRACEIO_DRIVER for debugging */
    status = (asynStatus)asynSetTraceIOMask(PORT_NAME, 0, 2);
    printf("Called asynSetTraceIOMask for UDP port, status=%d\n", status);
    status = (asynStatus)asynSetTraceMask(PORT_NAME, 0, 9);
    printf("Called asynSetTraceMask for UDP port, status=%d\n", status);
    
    /* Connect to the broadcast port */
    status = pasynOctetSyncIO->connect(PORT_NAME, 0, &pasynUser, NULL);
    printf("Connected to UDP port, status=%d\n", status);

    for (i=0; i<NLOOPS; i++) {
        /* Send the broadcast message */
        status = pasynOctetSyncIO->write(pasynUser, "i\n", 2, 1.0, &nwrite);
        printf("Wrote to UDP port, status=%d, nwrite=%d\n", status, (int)nwrite);

        /* Read the first response */
        status = pasynOctetSyncIO->read(pasynUser, buffer, sizeof(buffer), 0.1, &nread, &eomReason);
        printf("Read from UDP port, status=%d, nread=%d, buffer=\n%s\n\n", status, (int)nread, buffer);
    }
    asynReport(10, PORT_NAME);

#ifdef linux
     system("netstat -a | grep 164.54.160.255 | grep -v ca-2");
#endif

    return 0;
}
    
