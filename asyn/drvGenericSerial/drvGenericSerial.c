/**********************************************************************
* Asyn device support using generic serial line interfaces            *
**********************************************************************/       
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* gpibCore is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*
 * $Id: drvGenericSerial.c,v 1.6 2003-11-13 19:07:53 norume Exp $
 */

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <osiSock.h>
#include <cantProceed.h>
#include <errlog.h>
#include <iocsh.h>
#include <epicsAssert.h>
#include <epicsString.h>
#include <epicsThread.h>
#include <epicsTime.h>
#include <epicsTimer.h>
#include <epicsInterruptibleSyscall.h>
#include <osiUnistd.h>
#include <epicsExport.h>
#include <asynDriver.h>

#ifdef vxWorks
# include <tyLib.h>
# include <ioLib.h>
# include <sioLib.h>
# define CSTOPB STOPB
#else
# define HAVE_TERMIOS
# include <termios.h>
#endif

/*
 * This structure holds the hardware-specific information for a single
 * asyn link.  There is one for each serial line.
 */
typedef struct {
    char              *serialDeviceName;
    int                fd;
    int                isRemote;
    char              *eos;
    int                eoslen;
    int                eosCapacity;
    osiSockAddr        farAddr;
    epicsTimerQueueId  timerQueue;
    epicsTimerId       timer;
    epicsInterruptibleSyscallContext *interruptibleSyscallContext;
    int                wasClosed;
    int                needsDisconnect;
    int                needsDisconnectReported;
    epicsTimeStamp     whenClosed;
    int                openOnlyOnDisconnect;
    unsigned long      nReconnect;
    unsigned long      nRead;
    unsigned long      nWritten;
    int                setBaud;
    int                baud;
    int                cflag;
#ifdef HAVE_TERMIOS
    struct termios     termios;
#endif
} ttyController_t;

int drvGenericSerialDebug = 0;
epicsExportAddress(int,drvGenericSerialDebug);

/*
 * Show string in human-readable form
 */
static void
showString(const char *str, int numchars)
{
    while (numchars--) {
        char c = *str++;
        switch (c) {
        case '\n':  printf("\\n");  break;
        case '\r':  printf("\\r");  break;
        case '\t':  printf("\\t");  break;
        case '\\':  printf("\\\\");  break;
        default:
            if (isprint(c))
                printf("%c", c);   /* putchar(c) doesn't work on vxWorks (!!) */
            else
                printf("\\%03o", (unsigned char)c);
            break;
        }
    }
}

/*
 * Report link parameters
 */
static void
drvGenericSerialReport(void *drvPvt, FILE *fp, int details)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    fprintf(fp, "Serial line %s: %sonnected\n", tty->serialDeviceName,
                                                tty->fd >= 0 ? "C" : "Disc");
    if (details >= 1) {
        if (tty->fd >= 0)
            fprintf(fp, "                    fd: %d\n", tty->fd);
        fprintf(fp, "            Reconnects: %ld\n", tty->nReconnect);
        fprintf(fp, "    Characters written: %ld\n", tty->nWritten);
        fprintf(fp, "       Characters read: %ld\n", tty->nRead);
    }
}

/*
 * Silently close a connection
 */
static void
closeConnection(ttyController_t *tty)
{
    if (tty->fd >= 0) {
        epicsTimeGetCurrent(&tty->whenClosed);
        tty->wasClosed = 1;
        if (tty->openOnlyOnDisconnect)
            tty->needsDisconnect = 1;
        if (!epicsInterruptibleSyscallWasClosed(tty->interruptibleSyscallContext))
            close(tty->fd);
        tty->fd = -1;
    }
}

/*
 * Report error and close a connection
 */
static void
reportFailure(ttyController_t *tty, const char *caller, const char *reason)
{
      errlogPrintf("%s %s %s%s.\n",
                        caller,
                        tty->serialDeviceName,
                        reason,
                        tty->openOnlyOnDisconnect ?
                           " -- must be disconnected and reconnected" : "");
}

/*
 * Unblock the I/O operation
 */
static void
timeoutHandler(void *p)
{
    ttyController_t *tty = (ttyController_t *)p;

    if (drvGenericSerialDebug >= 3)
        printf("drvGenericSerial: %s timeout.\n", tty->serialDeviceName);
    epicsInterruptibleSyscallInterrupt(tty->interruptibleSyscallContext);
    /*
     * Since it is possible, though unlikely, that we got here before the
     * slow system call actually started, we arrange to poke the thread
     * again in a little while.
     */
    epicsTimerStartDelay(tty->timer, 10.0);
}

/*
 * Open connection initially or after timeout
 */
static asynStatus
openConnection (ttyController_t *tty)
{
    int i;

    /*
     * Sanity check
     */
    assert(tty);
    if (tty->fd >= 0) {
        errlogPrintf("drvGenericSerial: Link already open!\n");
        return asynError;
    }

    /*
     * Some devices require an explicit disconnect
     */
    if (tty->needsDisconnect) {
        if (!tty->needsDisconnectReported) {
            errlogPrintf("Can't open connection to %s till it has been explicitly disconnected.\n", tty->serialDeviceName);
            tty->needsDisconnectReported = 1;
        }
        return asynError;
    }
    
    /*
     * Create timeout mechanism
     */
    if ((tty->timerQueue == NULL)
     && ((tty->timerQueue = epicsTimerQueueAllocate(1, epicsThreadPriorityBaseMax)) == NULL)) {
        errlogPrintf("drvGenericSerial: Can't create timer queue.\n");
        return asynError;
    }
    if ((tty->timer == NULL) 
     && ((tty->timer = epicsTimerQueueCreateTimer(tty->timerQueue, timeoutHandler, tty)) == NULL)) {
        errlogPrintf("drvGenericSerial: Can't create timer.\n");
        return asynError;
    }
    if (tty->interruptibleSyscallContext == NULL)
        tty->interruptibleSyscallContext = epicsInterruptibleSyscallMustCreate("drvGenericSerial");

    /*
     * Don't try to open a connection too soon after breaking the previous one
     */
    if (tty->wasClosed) {
        epicsTimeStamp now;
        double sec;

        epicsTimeGetCurrent(&now);
        sec = epicsTimeDiffInSeconds(&now, &tty->whenClosed);
        if ((sec >= 0) && (sec < 2.0))
            epicsThreadSleep(2.0 - sec);
    }

    /*
     * Create the remote or local connection
     */
    if (drvGenericSerialDebug >= 1)
        printf("drvGenericSerial open connection to %s\n", tty->serialDeviceName);
    if (tty->isRemote) {
        /*
         * Create the socket
         */
        if ((tty->fd = epicsSocketCreate(PF_INET, SOCK_STREAM, 0)) < 0) {
            errlogPrintf("drvGenericSerial: Can't create socket: %s\n", strerror(errno));
            return asynError;
        }
        epicsInterruptibleSyscallArm(tty->interruptibleSyscallContext, tty->fd, epicsThreadGetIdSelf());

        /*
         * Connect to the remote terminal server
         */
        epicsTimerStartDelay(tty->timer, 10.0);
        i = connect(tty->fd, &tty->farAddr.sa, sizeof tty->farAddr.ia);
        epicsTimerCancel(tty->timer);
        if (epicsInterruptibleSyscallWasInterrupted(tty->interruptibleSyscallContext)) {
            errlogPrintf("drvGenericSerial: Timed out attempting to connect to %s\n", tty->serialDeviceName);
            if (!epicsInterruptibleSyscallWasClosed(tty->interruptibleSyscallContext))
                close(tty->fd);
            tty->fd = -1;
            return asynError;
        }
        if (i < 0) {
            errlogPrintf("drvGenericSerial: Can't connect to %s: %s\n", tty->serialDeviceName, strerror(errno));
            close(tty->fd);
            tty->fd = -1;
            return asynError;
        }
    }
    else {
        /*
         * Open serial line and get configuration
         * Must open in non-blocking mode in case carrier detect is not
         * present and we plan to use the line in CLOCAL mode.
         */
        if ((tty->fd = open(tty->serialDeviceName, O_RDWR|O_NOCTTY|O_NONBLOCK, 0)) < 0) {
            errlogPrintf("drvGenericSerial: Can't open `%s': %s\n", tty->serialDeviceName, strerror(errno));
            return asynError;
        }
        epicsInterruptibleSyscallArm(tty->interruptibleSyscallContext, tty->fd, epicsThreadGetIdSelf());
#ifdef HAVE_TERMIOS
        if (tcsetattr(tty->fd, TCSANOW, &tty->termios) < 0) {
            errlogPrintf("drvGenericSerial: Can't set `%s' attributes: %s\n", tty->serialDeviceName, strerror(errno));
            close(tty->fd);
            tty->fd = -1;
            return asynError;
        }
        tcflush(tty->fd, TCIOFLUSH);
#endif
#ifdef vxWorks
        if ((tty->setBaud)
         && (ioctl(tty->fd, FIOBAUDRATE, tty->baud) < 0)
         && (ioctl(tty->fd, SIO_BAUD_SET, tty->baud) < 0))
            errlogPrintf("Warning: `%s' supports neither FIOBAUDRATE nor SIO_BAUD_SET.\n", tty->serialDeviceName);
        if (ioctl(tty->fd, SIO_HW_OPTS_SET, tty->cflag) < 0)
            errlogPrintf("Warning: `%s' does not support SIO_HW_OPTS_SET.\n", tty->serialDeviceName);
#else
        /*
         * Turn off non-blocking mode
         */
        if (fcntl(tty->fd, F_SETFL, 0) < 0) {
            errlogPrintf("drvGenericSerial: Can't set `%s' file flags: %s\n", tty->serialDeviceName, strerror(errno));
            close(tty->fd);
            tty->fd = -1;
            return asynError;
        }
#endif
    }
    if (tty->wasClosed)
        tty->nReconnect++;
    if (drvGenericSerialDebug >= 1)
        printf("drvGenericSerial opened connection to %s\n", tty->serialDeviceName);
    return asynSuccess;
}

/*
 * Create a link
 */
static asynStatus
drvGenericSerialConnect(void *drvPvt, asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    return openConnection(tty);
}

static asynStatus
drvGenericSerialDisconnect(void *drvPvt, asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    if (drvGenericSerialDebug >= 1)
        printf("drvGenericSerial disconnect %s\n", tty->serialDeviceName);
    if (tty->timer)
        epicsTimerCancel(tty->timer);
    closeConnection(tty);
    tty->needsDisconnect = 0;
    tty->needsDisconnectReported = 0;
    return asynSuccess;
}

/*
 * Write to the serial line
 */
static int
drvGenericSerialWrite(void *drvPvt, asynUser *pasynUser, const char *data, int numchars)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int wrote;

    assert(tty);
    if (tty->fd < 0) {      
      if (tty->openOnlyOnDisconnect) return asynError;
      if (openConnection(tty) != asynSuccess) return asynError;
    }
    if (drvGenericSerialDebug >= 1) {
        printf("drvGenericSerialWrite %d ", numchars);
        if (drvGenericSerialDebug >= 2)
            showString(data, numchars);
        printf("\n");
    }
    epicsTimerStartDelay(tty->timer, pasynUser->timeout);
    wrote = write(tty->fd, data, numchars);
    epicsTimerCancel(tty->timer);
    if (epicsInterruptibleSyscallWasInterrupted(tty->interruptibleSyscallContext)) {
        reportFailure(tty, "drvGenericSerialWrite", "timeout");
        closeConnection(tty);
        return asynError;
    }
    if (wrote != numchars) {
        reportFailure(tty, "drvGenericSerialWrite", strerror(errno));
        closeConnection(tty);
        return -1;
    }
    tty->nWritten += numchars;
    return numchars;
}

/*
 * Read from the serial line
 * It is tempting to consider the use of cooked (ICANON) termios mode
 * to read characters when the EOS is a single character valid, but
 * there are a couple of reasons why this isn't a good idea.
 *  1) The cooked data inside the kernel is a fixed size.  A long
 *     input message could end up being truncated.
 *  2) If the EOS character were changed to -1 there could be problem
 *     with losing characters already on the cooked queue.
 * Question: When a read returns a positive value should we immediately
 *           return those character to our caller or should we loop
 *           until EOS is received or the character count is reached?
 *           For now I'm looping here.
 * Question: Given that we're looking for the EOS, if the EOS is found in
 *           the middle of a reply, what number of characters should be
 *           returned to our caller?  The total number of characters read
 *           or just the number up to and including the EOS?
 *               For now, I'm returning the latter.
 *           Should the extra characters be held till the next read?
 *               For now, I'm tossing them.
 */
static int
drvGenericSerialRead(void *drvPvt, asynUser *pasynUser, char *data, int maxchars)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int thisRead;
    int totalRead = 0;
    int eosMatched = 0;
    char *eosCheck = data;
    int nleft = 0;
    
    assert(tty);
    if (tty->fd < 0) {      
      if (tty->openOnlyOnDisconnect) return asynError;
      if (openConnection(tty) != asynSuccess) return asynError;
    }
    for (;;) {
        epicsTimerStartDelay(tty->timer, pasynUser->timeout);
        thisRead = read(tty->fd, data, maxchars);
        epicsTimerCancel(tty->timer);
        if (epicsInterruptibleSyscallWasInterrupted(tty->interruptibleSyscallContext)) {
            reportFailure(tty, "drvGenericSerialRead", "timeout");
            closeConnection(tty);
            return -1;
        }
        if (thisRead < 0) {
            reportFailure(tty, "drvGenericSerialRead", strerror(errno));
            closeConnection(tty);
            return -1;
        }
        if (thisRead == 0) {
            reportFailure(tty, "drvGenericSerialRead", "unexpected EOF");
            closeConnection(tty);
            return -1;
        }
        if (drvGenericSerialDebug >= 1) {
            printf("drvGenericSerialRead %d ", thisRead);
            if ((thisRead > 0) && (drvGenericSerialDebug >= 2))
                showString(data, thisRead);
            printf("\n");
        }
        totalRead += thisRead;
        maxchars -= thisRead;
        data += thisRead;
        if (maxchars == 0) {
            nleft = 0;
        }
        else if (tty->eoslen) {
            nleft = data - eosCheck - eosMatched;
            while (maxchars && nleft) {
                if (eosMatched == 0) {
                    char *cp = memchr(eosCheck, *tty->eos, nleft);
                    if (cp == NULL) {
                        eosCheck = data;
                        break;
                    }
                    else {
                        eosCheck = cp;
                        nleft = data - eosCheck - 1;
                        eosMatched = 1;
                    }
                }
                for (;;) {
                    if (eosMatched == tty->eoslen) {
                        maxchars = 0;
                        break;
                    }
                    if (nleft == 0) {
                        break;
                    }
                    nleft--;
                    if (tty->eos[eosMatched] != eosCheck[eosMatched]) {
                        eosCheck++;
                        eosMatched = 0;
                        break;
                    }
                    eosMatched++;
                }
            }
        }
        if (maxchars == 0) {
            if (nleft) {
                errlogPrintf("Ignoring %d extra character%s from %s.\n",
                          nleft, nleft == 1 ? "" : "s", tty->serialDeviceName);
                totalRead -= nleft;
            }
            tty->nRead += totalRead;
            return totalRead;
        }
    }
}

/*
 * Flush pending input
 */
static asynStatus
drvGenericSerialFlush(void *drvPvt,asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    if (tty->fd >= 0) {
#ifdef vxWorks
        ioctl(tty->fd, FIOCANCEL, 0);
#else
        tcflush(tty->fd, TCIOFLUSH);
#endif
    }
    return asynSuccess;
}

/*
 * Set the end-of-string message
 */
static asynStatus
drvGenericSerialSetEos(void *drvPvt,asynUser *pasynUser, const char *eos,int eoslen)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    assert(eoslen >= 0);
    if ((tty->eos == NULL)
     || (tty->eoslen != eoslen)
     || (memcmp(tty->eos, eos, eoslen) != 0)) {
        if (eoslen) {
            if (tty->eosCapacity < eoslen) {
                free(tty->eos);
                tty->eos = malloc(eoslen);
                if (tty->eos == NULL) {
                    tty->eosCapacity = 0;
                    tty->eoslen = 0;
                    errlogPrintf("devGenericSerial: Can't allocate memory for %s EOS\n", tty->serialDeviceName);
                    closeConnection(tty);
                    return asynError;
                }
                tty->eosCapacity = eoslen;
            }
            memcpy(tty->eos, eos, eoslen);
        }
        tty->eoslen = eoslen;
    }
    return asynSuccess;
}

/*
 * Clean up a ttyController
 */
static void
ttyCleanup(ttyController_t *tty)
{
    if (tty) {
        if (tty->fd >= 0)
            close(tty->fd);
        free(tty->serialDeviceName);
        free(tty);
    }
}

/*
 * asynCommon methods
 */
static const struct asynCommon drvGenericSerialAsynCommon = {
    drvGenericSerialReport,
    drvGenericSerialConnect,
    drvGenericSerialDisconnect
};

/*
 * asynOctet methods
 */
static const struct asynOctet drvGenericSerialAsynOctet = {
    drvGenericSerialRead,
    drvGenericSerialWrite,
    drvGenericSerialFlush,
    drvGenericSerialSetEos
};

/*
 * Configure and register a generic serial device
 */
static int
drvGenericSerialConf(char *portName,
                     char *ttyName,
                     unsigned int priority,
                     int openOnlyOnDisconnect,
                     int argc, char **argv)
{
    int i;
    ttyController_t *tty;
    asynInterface *pasynInterface;
    char *cp;
    char *arg;

    /*
     * Check arguments
     */
    if (portName == NULL) {
        errlogPrintf("Port name missing.\n");
        return -1;
    }
    if (ttyName == NULL) {
        errlogPrintf("TTY name missing.\n");
        return -1;
    }

    /*
     * Create a driver
     */
    tty = (ttyController_t *)callocMustSucceed(1, sizeof *tty, "drvGenericSerialConfigure()");
    tty->fd = -1;
    tty->serialDeviceName = epicsStrDup(ttyName);
    tty->baud = -1;

    /*
     * Parse configuration parameters
     */
    if ((cp = strchr(tty->serialDeviceName, ':')) != NULL) {
        int port;

        *cp = '\0';
        if(hostToIPAddr(tty->serialDeviceName, &tty->farAddr.ia.sin_addr) < 0) {
            *cp = ':';
            errlogPrintf("drvGenericSerialConfigure: Unknown host '%s'.\n", tty->serialDeviceName);
            ttyCleanup(tty);
            return -1;
        }
        port = strtol(cp+1, NULL, 0);
        tty->farAddr.ia.sin_port = htons(port);
        tty->farAddr.ia.sin_family = AF_INET;
        memset(tty->farAddr.ia.sin_zero, 0, sizeof tty->farAddr.ia.sin_zero);
        *cp = ':';
        tty->isRemote = 1;
    }
    tty->openOnlyOnDisconnect = openOnlyOnDisconnect;

    /*
     * Parse stty parameters
     */
    if ((argc > 1) && tty->isRemote)
        printf ("Warning -- stty parameters are ignored for remote terminal connections.\n");
    tty->cflag = CREAD | CLOCAL | CS8;
    for (i = 1 ; i < argc ; i++) {
        arg = argv[i];
        if (isdigit(*arg)) {
            tty->baud = 0;
            while (isdigit(*arg)) {
                if (tty->baud < 100000)
                    tty->baud = tty->baud * 10 + (*arg - '0');
                arg++;
            }
            if (*arg != '\0') {
                errlogPrintf("Invalid speed\n");
                return -1;
            }
        }
        else if (strcmp(arg, "cs5") == 0) {
            tty->cflag = (tty->cflag & ~CSIZE) | CS5;
        }
        else if (strcmp(arg, "cs6") == 0) {
            tty->cflag = (tty->cflag & ~CSIZE) | CS6;
        }
        else if (strcmp(arg, "cs7") == 0) {
            tty->cflag = (tty->cflag & ~CSIZE) | CS7;
        }
        else if (strcmp(arg, "cs8") == 0) {
            tty->cflag = (tty->cflag & ~CSIZE) | CS8;
        }
        else if (strcmp(arg, "parenb") == 0) {
            tty->cflag |= PARENB;
        }
        else if (strcmp(arg, "-parenb") == 0) {
            tty->cflag &= ~PARENB;
        }
        else if (strcmp(arg, "parodd") == 0) {
            tty->cflag |= PARODD;
        }
        else if (strcmp(arg, "-parodd") == 0) {
            tty->cflag &= ~PARODD;
        }
        else if (strcmp(arg, "clocal") == 0) {
            tty->cflag |= CLOCAL;
        }
        else if (strcmp(arg, "-clocal") == 0) {
            tty->cflag &= ~CLOCAL;
        }
        else if (strcmp(arg, "cstopb") == 0) {
            tty->cflag |= CSTOPB;
        }
        else if (strcmp(arg, "-cstopb") == 0) {
            tty->cflag &= ~CSTOPB;
        }
        else if (strcmp(arg, "crtscts") == 0) {
#if defined(CRTSCTS)
            tty->cflag |= CRTSCTS;
#else
            errlogPrintf("Warning -- RTS/CTS flow control is not available on this machine.\n");
#endif
        }
        else if (strcmp(arg, "-crtscts") == 0) {
#if defined(CRTSCTS)
            tty->cflag &= ~CRTSCTS;
#endif
        }
        else {
            errlogPrintf("stty: Warning -- Unsupported option `%s'\n", arg);
        }
    }
#ifdef HAVE_TERMIOS
    tty->termios.c_cflag = tty->cflag;
    tty->termios.c_iflag = IGNBRK | IGNPAR;
    tty->termios.c_oflag = 0;
    tty->termios.c_lflag = 0;
    tty->termios.c_cc[VMIN] = 1;
    tty->termios.c_cc[VTIME] = 0;
    cfsetispeed(&tty->termios,B9600);
    cfsetospeed(&tty->termios,B9600);
    switch(tty->baud) {
    case -1:    break;
    case 50:    cfsetispeed(&tty->termios,B50);    cfsetospeed(&tty->termios,B50);     break;
    case 75:    cfsetispeed(&tty->termios,B75);    cfsetospeed(&tty->termios,B75);     break;
    case 110:   cfsetispeed(&tty->termios,B110);   cfsetospeed(&tty->termios,B110);    break;
    case 134:   cfsetispeed(&tty->termios,B134);   cfsetospeed(&tty->termios,B134);    break;
    case 150:   cfsetispeed(&tty->termios,B150);   cfsetospeed(&tty->termios,B150);    break;
    case 200:   cfsetispeed(&tty->termios,B200);   cfsetospeed(&tty->termios,B200);    break;
    case 300:   cfsetispeed(&tty->termios,B300);   cfsetospeed(&tty->termios,B300);    break;
    case 600:   cfsetispeed(&tty->termios,B600);   cfsetospeed(&tty->termios,B600);    break;
    case 1200:  cfsetispeed(&tty->termios,B1200);  cfsetospeed(&tty->termios,B1200);   break;
    case 1800:  cfsetispeed(&tty->termios,B1800);  cfsetospeed(&tty->termios,B1800);   break;
    case 2400:  cfsetispeed(&tty->termios,B2400);  cfsetospeed(&tty->termios,B2400);   break;
    case 4800:  cfsetispeed(&tty->termios,B4800);  cfsetospeed(&tty->termios,B4800);   break;
    case 9600:  cfsetispeed(&tty->termios,B9600);  cfsetospeed(&tty->termios,B9600);   break;
    case 19200: cfsetispeed(&tty->termios,B19200); cfsetospeed(&tty->termios,B19200);  break;
    case 38400: cfsetispeed(&tty->termios,B38400); cfsetospeed(&tty->termios,B38400);  break;
    case 57600: cfsetispeed(&tty->termios,B57600); cfsetospeed(&tty->termios,B57600);  break;
    case 115200:cfsetispeed(&tty->termios,B115200);cfsetospeed(&tty->termios,B115200); break;
    case 230400:cfsetispeed(&tty->termios,B230400);cfsetospeed(&tty->termios,B230400); break;
    default:
        errlogPrintf("Invalid speed.\n");
        return -1;
    }
#endif

    /*
     *  Link with higher level routines
     */
    pasynInterface = (asynInterface *)callocMustSucceed(2, sizeof *pasynInterface, "drvGenericSerialConfigure");
    pasynInterface[0].interfaceType = asynCommonType;
    pasynInterface[0].pinterface = (void *)&drvGenericSerialAsynCommon;
    pasynInterface[0].drvPvt = tty;
    pasynInterface[1].interfaceType = asynOctetType;
    pasynInterface[1].pinterface = (void *)&drvGenericSerialAsynOctet;
    pasynInterface[1].drvPvt = tty;
    if (pasynManager->registerPort(epicsStrDup(portName),
                                   pasynInterface,
                                   2,
                                   priority,
                                   0) != asynSuccess) {
        errlogPrintf("drvGenericSerialConfigure: Can't register myself.\n");
        ttyCleanup(tty);
        return -1;
    }
    return 0;
}

/*
 * Entry point from vxWorks shell
 */
int
drvGenericSerialConfigure(char *portName,
                          char *ttyName,
                          unsigned int priority,
                          int openOnlyOnDisconnect,
                          char *a1, char *a2, char *a3,
                          char *a4, char *a5, char *a6)
{
    int argc = 0;
    char *argv[7];

    argv[argc++] = "stty";
    if (a1) {
        argv[argc++] = a1;
        if (a2) {
            argv[argc++] = a2;
            if (a3) {
                argv[argc++] = a3;
                if (a4) {
                    argv[argc++] = a4;
                    if (a5) {
                        argv[argc++] = a5;
                        if (a6) {
                            argv[argc++] = a6;
                        }
                    }
                }
            }
        }
    }
    argv[argc] = NULL;
    return drvGenericSerialConf(portName, ttyName, priority,
                                openOnlyOnDisconnect, argc, argv);
}

/*
 * IOC shell command registration
 */
#include <iocsh.h>
static const iocshArg drvGenericSerialConfigureArg0 = { "port name",iocshArgString};
static const iocshArg drvGenericSerialConfigureArg1 = { "tty name",iocshArgString};
static const iocshArg drvGenericSerialConfigureArg2 = { "priority",iocshArgInt};
static const iocshArg drvGenericSerialConfigureArg3 = { "reopen only after disconnect",iocshArgInt};
static const iocshArg drvGenericSerialConfigureArg4 = { "[stty options]",iocshArgArgv};
static const iocshArg *drvGenericSerialConfigureArgs[] = {
    &drvGenericSerialConfigureArg0, &drvGenericSerialConfigureArg1,
    &drvGenericSerialConfigureArg2, &drvGenericSerialConfigureArg3,
    &drvGenericSerialConfigureArg4};
static const iocshFuncDef drvGenericSerialConfigureFuncDef =
                      {"drvGenericSerialConfigure",5,drvGenericSerialConfigureArgs};
static void drvGenericSerialConfigureCallFunc(const iocshArgBuf *args)
{
    drvGenericSerialConf(args[0].sval, args[1].sval, args[2].ival, args[3].ival,
                         args[4].aval.ac, args[4].aval.av);
}

/*
 * This routine is called before multitasking has started, so there's
 * no race condition in the test/set of firstTime.
 */
static void 
drvGenericSerialRegisterCommands(void)
{
    static int firstTime = 1;
    if (firstTime) {
        iocshRegister(&drvGenericSerialConfigureFuncDef,drvGenericSerialConfigureCallFunc);
        firstTime = 0;
    }
}
epicsExportRegistrar(drvGenericSerialRegisterCommands);
