/*
 * testFtdiMain.cpp
 *
 * Program to test drvAsynFtdiPort.
 * This test program assumes that there is an FTDI device
 * connected to a USB port and that it is wired as a loopback
 * (just echoing back what is sent).
 *
 * Author: Bruno Martins
 */

#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include <asynOctetSyncIO.h>
#include <drvAsynFTDIPort.h>
#include <asynShellCommands.h>
#include <epicsThread.h>

const char *PORT_NAME = "PORT";
const int VENDOR = 0x0403;
const int PRODUCT = 0x6001;
const int BAUDRATE = 1000000;
const int LATENCY = 2;
const double TIMEOUT = 3.0;

int main(int argc, char **argv)
{
    asynStatus status;
    size_t nwrite, nread;
    int eomReason;
    asynUser *pasynUser;

    status = (asynStatus) drvAsynFTDIPortConfigure(
        PORT_NAME, VENDOR, PRODUCT, BAUDRATE, LATENCY, 0, 0, 0
    );

    printf("drvAsynFTDIPortConfigure(port='%s', vendor=0x%04X, product=0x%04X, baudrate=%d, latency=%d) -> %d\n",
        PORT_NAME, VENDOR, PRODUCT, BAUDRATE, LATENCY, status
    );

    // Enable Tracing
    /*asynSetTraceInfoMask(PORT_NAME,-1,0x03);
    asynSetTraceIOMask(PORT_NAME, -1, 0xFF);
    asynSetTraceMask(PORT_NAME, -1, 0xFF);*/

    status = pasynOctetSyncIO->connect(PORT_NAME, 0, &pasynUser, NULL);

    // Wait for connection
    epicsTimeStamp start;
    epicsTimeGetCurrent(&start);
    double elapsed = 0.0;
    int connected = 0;

    while (!connected) {
        epicsTimeStamp now;
        epicsTimeGetCurrent(&now);
        double elapsed = epicsTimeDiffInSeconds(&now, &start);
        pasynManager->isConnected(pasynUser, &connected);

        if (connected || elapsed >= TIMEOUT)
            break;

        epicsThreadSleep(0.1);
    }

    if (!connected) {
        fprintf(stderr, "Timed-out while waiting for connection\n");
        return EXIT_FAILURE;
    }

    printf("Connected to FTDI port, status=%d (took=%.3f sec)\n",
        status, elapsed
    );

    char wbuf[256], rbuf[256];
    int ok;

    // Test sending strings
    snprintf(wbuf, sizeof(wbuf), "Hello!");
    status = pasynOctetSyncIO->writeRead(
        pasynUser, wbuf, strlen(wbuf)+1, rbuf, sizeof(rbuf),
        1.0, &nwrite, &nread, &eomReason
    );

    ok = !strncmp(wbuf, rbuf, sizeof(wbuf));
    printf("Wrote (%lu) '%s', received (%lu) '%s' --> %sOK!\n",
        nwrite, wbuf, nread, rbuf, ok ? "" : "NOT ");

    // Test sending binary data
    wbuf[0] = 0xAA;
    wbuf[1] = 0xBB;
    wbuf[2] = 0xCC;

    status = pasynOctetSyncIO->writeRead(
        pasynUser, wbuf, 3, rbuf, 3,
        1.0, &nwrite, &nread, &eomReason
    );

    ok = nwrite == nread;
    for (size_t i = 0; ok && i < nwrite; ++i)
        ok &= wbuf[i] == rbuf[i];

    printf("Wrote (%lu) '", nwrite);
    for (size_t i = 0; i < nwrite; ++i)
        printf("%02X ", (unsigned char)wbuf[i]);
    printf("', received (%lu) '", nread);
    for (size_t i = 0; i < nread; ++i)
        printf("%02X ", (unsigned char)rbuf[i]);
    printf("' --> %sOK!\n", ok ? "" : "NOT ");

    asynReport(10, PORT_NAME);

    return EXIT_SUCCESS;
}
