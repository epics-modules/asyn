/**********************************************************************
* Asyn device support using generic serial line interfaces            *
**********************************************************************/       
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*
 * $Id: drvGenericSerial.c,v 1.11 2004-01-13 22:40:31 norume Exp $
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

/* FIXME: */
#define epicsStrCaseCmp strcasecmp
/*
 * This structure holds the hardware-specific information for a single
 * asyn link.  There is one for each serial line.
 */
typedef struct {
    asynUser          *pasynUser;
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
    int                baud;
    int                baudCode;
    int                cflag;
} ttyController_t;

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

    asynPrint(tty->pasynUser, ASYN_TRACE_FLOW,
               "drvGenericSerial: %s timeout.\n", tty->serialDeviceName);
    epicsInterruptibleSyscallInterrupt(tty->interruptibleSyscallContext);
    /*
     * Since it is possible, though unlikely, that we got here before the
     * slow system call actually started, we arrange to poke the thread
     * again in a little while.
     */
    epicsTimerStartDelay(tty->timer, 10.0);
}

/*
 * Set serial line I/O mode
 */
static asynStatus
setMode(ttyController_t *tty)
{
#ifdef HAVE_TERMIOS
    struct termios     termios;
    termios.c_cflag = tty->cflag;
    termios.c_iflag = IGNBRK | IGNPAR;
    termios.c_oflag = 0;
    termios.c_lflag = 0;
    termios.c_cc[VMIN] = 1;
    termios.c_cc[VTIME] = 0;
    cfsetispeed(&termios,tty->baudCode);
    cfsetospeed(&termios,tty->baudCode);
    if (tcsetattr(tty->fd, TCSADRAIN, &termios) < 0) {
        errlogPrintf("drvGenericSerial: Can't set `%s' attributes: %s\n", tty->serialDeviceName, strerror(errno));
        return asynError;
    }
    return asynSuccess;
#endif
#ifdef vxWorks
    if (ioctl(tty->fd, SIO_HW_OPTS_SET, tty->cflag) < 0)
        errlogPrintf("Warning: `%s' does not support SIO_HW_OPTS_SET.\n", tty->serialDeviceName);
    return asynSuccess;
#endif
    errlogPrintf("Warning: drvGenericSerial doesn't know how to set serial port mode on this machine.\n");
    return asynSuccess;
}

/*
 * Set serial line speed
 */
static asynStatus
setBaud (ttyController_t *tty)
{
#ifdef HAVE_TERMIOS
    return  setMode(tty);
#endif
#ifdef vxWorks
    if ((ioctl(tty->fd, FIOBAUDRATE, tty->baud) < 0)
     && (ioctl(tty->fd, SIO_BAUD_SET, tty->baud) < 0))
        errlogPrintf("Warning: `%s' supports neither FIOBAUDRATE nor SIO_BAUD_SET.\n", tty->serialDeviceName);
    return asynSuccess;
#endif
    errlogPrintf("Warning: drvGenericSerial doesn't know how to set serial port mode on this machine.\n");
    return asynSuccess; 
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
    asynPrint(tty->pasynUser, ASYN_TRACE_FLOW,
          "drvGenericSerial open connection to %s\n", tty->serialDeviceName);
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
         * Open serial line and set configuration
         * Must open in non-blocking mode in case carrier detect is not
         * present and we plan to use the line in CLOCAL mode.
         */
        if ((tty->fd = open(tty->serialDeviceName, O_RDWR|O_NOCTTY|O_NONBLOCK, 0)) < 0) {
            errlogPrintf("drvGenericSerial: Can't open `%s': %s\n", tty->serialDeviceName, strerror(errno));
            return asynError;
        }
        epicsInterruptibleSyscallArm(tty->interruptibleSyscallContext, tty->fd, epicsThreadGetIdSelf());
        if ((setBaud(tty) != asynSuccess)
         || (setMode(tty) != asynSuccess)) {
            close(tty->fd);
            tty->fd = -1;
            return asynError;
        }
#ifndef vxWorks
        /*
         * Turn off non-blocking mode
         */
        tcflush(tty->fd, TCIOFLUSH);
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
    asynPrint(tty->pasynUser, ASYN_TRACE_FLOW,
          "drvGenericSerial opened connection to %s\n", tty->serialDeviceName);
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
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
               "drvGenericSerial: %s connect.\n", tty->serialDeviceName);
    tty->pasynUser = pasynUser;
    return openConnection(tty);
}

static asynStatus
drvGenericSerialDisconnect(void *drvPvt, asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
            "drvGenericSerial disconnect %s\n", tty->serialDeviceName);
    if (tty->timer)
        epicsTimerCancel(tty->timer);
    closeConnection(tty);
    tty->needsDisconnect = 0;
    tty->needsDisconnectReported = 0;
    return asynSuccess;
}

static asynStatus
drvGenericSerialSetPortOption(void *drvPvt, const char *key, const char *val)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int baud = 0;

    assert(tty);
    if (tty->isRemote) {
        printf ("Warning -- port option parameters are ignored for remote terminal connections.\n");
        return asynSuccess;
    }
    if (epicsStrCaseCmp(key, "baud") == 0) {
        while (isdigit(*val)) {
            if (baud < 100000)
                baud = baud * 10 + (*val - '0');
            val++;
        }
        if (*val != '\0')
            baud = 0;
        switch (baud) {
        case 50:    tty->baudCode = B50;     break;
        case 75:    tty->baudCode = B75;     break;
        case 110:   tty->baudCode = B110;    break;
        case 134:   tty->baudCode = B134;    break;
        case 150:   tty->baudCode = B150;    break;
        case 200:   tty->baudCode = B200;    break;
        case 300:   tty->baudCode = B300;    break;
        case 600:   tty->baudCode = B600;    break;
        case 1200:  tty->baudCode = B1200;   break;
        case 1800:  tty->baudCode = B1800;   break;
        case 2400:  tty->baudCode = B2400;   break;
        case 4800:  tty->baudCode = B4800;   break;
        case 9600:  tty->baudCode = B9600;   break;
        case 19200: tty->baudCode = B19200;  break;
        case 38400: tty->baudCode = B38400;  break;
        case 57600: tty->baudCode = B57600;  break;
        case 115200:tty->baudCode = B115200; break;
        case 230400:tty->baudCode = B230400; break;
        default:
            errlogPrintf("Invalid speed.\n");
            return asynError;
        }
        tty->baud = baud;
    }
    else if (epicsStrCaseCmp(key, "bits") == 0) {
        if (epicsStrCaseCmp(val, "5") == 0) {
            tty->cflag = (tty->cflag & ~CSIZE) | CS5;
        }
        else if (epicsStrCaseCmp(val, "6") == 0) {
            tty->cflag = (tty->cflag & ~CSIZE) | CS6;
        }
        else if (epicsStrCaseCmp(val, "7") == 0) {
            tty->cflag = (tty->cflag & ~CSIZE) | CS7;
        }
        else if (epicsStrCaseCmp(val, "8") == 0) {
            tty->cflag = (tty->cflag & ~CSIZE) | CS8;
        }
        else {
            errlogPrintf("Invalid number of bits.\n");
            return asynError;
        }
    }
    else if (epicsStrCaseCmp(key, "parity") == 0) {
        if (epicsStrCaseCmp(val, "none") == 0) {
            tty->cflag &= ~PARENB;
        }
        else if (epicsStrCaseCmp(val, "even") == 0) {
            tty->cflag |= PARENB;
            tty->cflag &= ~PARODD;
        }
        else if (epicsStrCaseCmp(val, "arodd") == 0) {
            tty->cflag |= PARENB;
            tty->cflag |= PARODD;
        }
        else {
            errlogPrintf("Invalid parity.\n");
            return asynError;
        }
    }
    else if (epicsStrCaseCmp(key, "stop") == 0) {
        if (epicsStrCaseCmp(val, "1") == 0) {
            tty->cflag &= ~CSTOPB;
        }
        else if (epicsStrCaseCmp(val, "2") == 0) {
            tty->cflag |= CSTOPB;
        }
        else {
            errlogPrintf("Invalid number of stop bits.\n");
            return asynError;
        }
    }
    else if (epicsStrCaseCmp(key, "clocal") == 0) {
        if (epicsStrCaseCmp(val, "Y") == 0) {
            tty->cflag |= CLOCAL;
        }
        else if (epicsStrCaseCmp(val, "N") == 0) {
            tty->cflag &= ~CLOCAL;
        }
        else {
            errlogPrintf("Invalid clocal value.\n");
            return asynError;
        }
    }
    else if (epicsStrCaseCmp(key, "crtscts") == 0) {
        if (epicsStrCaseCmp(val, "Y") == 0) {
#if defined(CRTSCTS)
            tty->cflag |= CRTSCTS;
#else
            errlogPrintf("Warning -- RTS/CTS flow control is not available on this machine.\n");
#endif
        }
        else if (epicsStrCaseCmp(val, "N") == 0) {
#if defined(CRTSCTS)
            tty->cflag &= ~CRTSCTS;
#endif
        }
        else {
            errlogPrintf("Invalid crtscts value.\n");
            return asynError;
        }
    }
    else {
        errlogPrintf("stty: Warning -- Unsupported option `%s'\n", key);
        return asynError;
    }
    if (tty->fd >= 0) {
        if (baud)
            return setBaud(tty);
        else
            return setMode(tty);
    }
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
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
               "drvGenericSerial: %s write.\n", tty->serialDeviceName);
    if (tty->fd < 0) {
      if (tty->openOnlyOnDisconnect) return asynError;
      if (openConnection(tty) != asynSuccess) return asynError;
    }
    asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, data, numchars,
               "drvGenericSerialWrite %d ", numchars);
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
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
               "drvGenericSerial: %s read.\n", tty->serialDeviceName);
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
        asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, data, thisRead,
                   "drvGenericSerialRead %d ", thisRead);
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
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
            "drvGenericSerial flush %s\n", tty->serialDeviceName);
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
    asynPrintIO(pasynUser, ASYN_TRACE_FLOW, eos, eoslen,
            "drvGenericSerial set eos %d ", eoslen);
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
    drvGenericSerialDisconnect,
    drvGenericSerialSetPortOption
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
/* Dont make this statis so that it can be cxalled by vxWorks shell */
int
drvGenericSerialConfigure(char *portName,
                     char *ttyName,
                     unsigned int priority,
                     int openOnlyOnDisconnect)
{
    ttyController_t *tty;
    asynInterface *pasynInterface;
    char *cp;

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
    tty->baud = 9600;
    tty->baudCode = B9600;
    tty->cflag = CREAD | CLOCAL | CS8;

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
 * IOC shell command registration
 */
#include <iocsh.h>
static const iocshArg drvGenericSerialConfigureArg0 = { "port name",iocshArgString};
static const iocshArg drvGenericSerialConfigureArg1 = { "tty name",iocshArgString};
static const iocshArg drvGenericSerialConfigureArg2 = { "priority",iocshArgInt};
static const iocshArg drvGenericSerialConfigureArg3 = { "reopen only after disconnect",iocshArgInt};
static const iocshArg *drvGenericSerialConfigureArgs[] = {
    &drvGenericSerialConfigureArg0, &drvGenericSerialConfigureArg1,
    &drvGenericSerialConfigureArg2, &drvGenericSerialConfigureArg3};
static const iocshFuncDef drvGenericSerialConfigureFuncDef =
                      {"drvGenericSerialConfigure",4,drvGenericSerialConfigureArgs};
static void drvGenericSerialConfigureCallFunc(const iocshArgBuf *args)
{
    drvGenericSerialConfigure(args[0].sval,args[1].sval,args[2].ival,args[3].ival);
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
