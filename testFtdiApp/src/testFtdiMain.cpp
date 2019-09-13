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
#include <asynOptionSyncIO.h>
#include <asynPortClient.h>
#include <drvAsynFTDIPort.h>
#include <asynShellCommands.h>
#include <epicsThread.h>

const char *PORT_NAME = "PORT";
const int VENDOR = 0x0403;
const int PRODUCT = 0x6001;
const int BAUDRATE = 1000000;
const int LATENCY = 2;
const double TIMEOUT = 1.0;

int main(int argc, char **argv)
{
    asynStatus status;

    status = (asynStatus) drvAsynFTDIPortConfigure(
        PORT_NAME, VENDOR, PRODUCT, BAUDRATE, LATENCY, 0, 0, 1
    );

    printf("drvAsynFTDIPortConfigure(port='%s', vendor=0x%04X, product=0x%04X, baudrate=%d, latency=%d) -> %d\n",
        PORT_NAME, VENDOR, PRODUCT, BAUDRATE, LATENCY, status
    );

    // Enable Tracing
    /*asynSetTraceInfoMask(PORT_NAME,-1,0x03);
    asynSetTraceIOMask(PORT_NAME, -1, 0xFF);
    asynSetTraceMask(PORT_NAME, -1, 0xFF);*/

    asynOctetClient oct(PORT_NAME, 0);
    asynOptionClient opt(PORT_NAME, 0);

    oct.setTimeout(TIMEOUT);

    // Test sending strings at different baud rates and different port settings
    const char *bauds[] = {
        "9600", "14400", "19200",
        "38400", "57600", "115200",
        "1000000", "3000000"
    };
    size_t nbauds = sizeof(bauds)/sizeof(bauds[0]);

    for (size_t i = 0; i < nbauds; ++i) {
        size_t nwrite, nread;
        int eomReason;
        char actualBaud[64] = {};
        char wbuf[256], rbuf[256];
        int ok;

        status = opt.setOption("baud", bauds[i]);

        if (status) {
            fprintf(stderr, "Failed to set baud rate\n");
            continue;
        }

        status = opt.getOption("baud", actualBaud, sizeof(actualBaud));

        if (status) {
            fprintf(stderr, "Failed to retrieve baud rate\n");
            continue;
        }

        printf("Set baud rate to %s (actual: %s)\n", bauds[i], actualBaud);

        int len = snprintf(wbuf, sizeof(wbuf), "Hello!");

        status = oct.writeRead(wbuf, len+1, rbuf, len+1, &nwrite, &nread, &eomReason);

        if (status) {
            fprintf(stderr, "  writeRead(string) failed: %d\n", status);
            continue;
        }

        ok = !strncmp(wbuf, rbuf, sizeof(wbuf));
        printf("  Wrote (%lu) '%s', received (%lu) '%s' --> %sOK!\n",
            nwrite, wbuf, nread, rbuf, ok ? "" : "NOT ");

        // Test sending binary data
        wbuf[0] = 0x00;
        wbuf[1] = 0xAB;
        wbuf[2] = 0xCD;

        status = oct.writeRead(
            wbuf, 3, rbuf, 3, &nwrite, &nread, &eomReason
        );

        if (status) {
            fprintf(stderr, "  writeRead(binary) failed: %d\n", status);
            continue;
        }

        ok = nwrite == nread;
        for (size_t j = 0; ok && j < nwrite; ++j)
            ok &= wbuf[j] == rbuf[j];

        printf("  Wrote (%lu) '", nwrite);
        for (size_t j = 0; j < nwrite; ++j)
            printf("%02X ", (unsigned char)wbuf[j]);
        printf("', received (%lu) '", nread);
        for (size_t j = 0; j < nread; ++j)
            printf("%02X ", (unsigned char)rbuf[j]);
        printf("' --> %sOK!\n", ok ? "" : "NOT ");
    }

    //asynReport(10, PORT_NAME);

    return EXIT_SUCCESS;
}
