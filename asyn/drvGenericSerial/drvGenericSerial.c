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
 * $Id: drvGenericSerial.c,v 1.20 2004-01-22 18:53:00 norume Exp $
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
#include <epicsStdio.h>
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

#define INBUFFER_SIZE   600

/*
 * This structure holds the hardware-specific information for a single
 * asyn link.  There is one for each serial line.
 */
typedef struct {
    asynUser          *pasynUser;
    char              *serialDeviceName;
    int                fd;
    int                isRemote;
    char               eos[2];
    int                eoslen;
    char               inBuffer[INBUFFER_SIZE];
    unsigned int       inBufferHead;
    unsigned int       inBufferTail;
    int                eosMatch;
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
        fprintf(fp, "            Reconnects: %lu\n", tty->nReconnect);
        fprintf(fp, "    Characters written: %lu\n", tty->nWritten);
        fprintf(fp, "       Characters read: %lu\n", tty->nRead);
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
      asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
                "%s %s %s%s.\n",
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
#if defined(HAVE_TERMIOS)
    struct termios termios;
    int baudCode;
    switch (tty->baud) {
    case 50:    baudCode = B50;     break;
    case 75:    baudCode = B75;     break;
    case 110:   baudCode = B110;    break;
    case 134:   baudCode = B134;    break;
    case 150:   baudCode = B150;    break;
    case 200:   baudCode = B200;    break;
    case 300:   baudCode = B300;    break;
    case 600:   baudCode = B600;    break;
    case 1200:  baudCode = B1200;   break;
    case 1800:  baudCode = B1800;   break;
    case 2400:  baudCode = B2400;   break;
    case 4800:  baudCode = B4800;   break;
    case 9600:  baudCode = B9600;   break;
    case 19200: baudCode = B19200;  break;
    case 38400: baudCode = B38400;  break;
    case 57600: baudCode = B57600;  break;
    case 115200:baudCode = B115200; break;
    case 230400:baudCode = B230400; break;
    default:
        asynPrint(tty->pasynUser, ASYN_TRACE_ERROR, "Invalid speed.\n");
        return asynError;
    }
    termios.c_cflag = tty->cflag;
    termios.c_iflag = IGNBRK | IGNPAR;
    termios.c_oflag = 0;
    termios.c_lflag = 0;
    termios.c_cc[VMIN] = 1;
    termios.c_cc[VTIME] = 0;
    cfsetispeed(&termios,baudCode);
    cfsetospeed(&termios,baudCode);
    if (tcsetattr(tty->fd, TCSADRAIN, &termios) < 0) {
        asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
                  "drvGenericSerial: Can't set `%s' attributes: %s\n",
                       tty->serialDeviceName, strerror(errno));
        return asynError;
    }
    return asynSuccess;
#elif defined(vxWorks)
    if (ioctl(tty->fd, SIO_HW_OPTS_SET, tty->cflag) < 0)
        asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
                  "Warning: `%s' does not support SIO_HW_OPTS_SET.\n",
                    tty->serialDeviceName);
    return asynSuccess;
#else
    asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
              "Warning: drvGenericSerial doesn't know how to set serial port mode on this machine.\n");
    return asynSuccess;
#endif
}

/*
 * Set serial line speed
 */
static asynStatus
setBaud (ttyController_t *tty)
{
#if defined(HAVE_TERMIOS)
    return  setMode(tty);
#elif defined(vxWorks)
    if ((ioctl(tty->fd, FIOBAUDRATE, tty->baud) < 0)
     && (ioctl(tty->fd, SIO_BAUD_SET, tty->baud) < 0))
        asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
                  "Warning: `%s' supports neither FIOBAUDRATE nor SIO_BAUD_SET.\n",
                      tty->serialDeviceName);
    return asynSuccess;
#else
    asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
              "Warning: drvGenericSerial doesn't know how to set serial port mode on this machine.\n");
    return asynSuccess; 
#endif
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
        asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
                  "drvGenericSerial: Link already open!\n");
        return asynError;
    }

    /*
     * Some devices require an explicit disconnect
     */
    if (tty->needsDisconnect) {
        if (!tty->needsDisconnectReported) {
            asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
                      "Can't open connection to %s till it has been explicitly disconnected.\n",
                        tty->serialDeviceName);
            tty->needsDisconnectReported = 1;
        }
        return asynError;
    }
    
    /*
     * Create timeout mechanism
     */
    if ((tty->timerQueue == NULL)
     && ((tty->timerQueue = epicsTimerQueueAllocate(1, epicsThreadPriorityBaseMax)) == NULL)) {
        asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
                  "drvGenericSerial: Can't create timer queue.\n");
        return asynError;
    }
    if ((tty->timer == NULL) 
     && ((tty->timer = epicsTimerQueueCreateTimer(tty->timerQueue, timeoutHandler, tty)) == NULL)) {
        asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
                  "drvGenericSerial: Can't create timer.\n");
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
            asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
                      "drvGenericSerial: Can't create socket: %s\n",
                           strerror(errno));
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
            asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
                      "drvGenericSerial: Timed out attempting to connect to %s\n",
                        tty->serialDeviceName);
            if (!epicsInterruptibleSyscallWasClosed(tty->interruptibleSyscallContext))
                close(tty->fd);
            tty->fd = -1;
            return asynError;
        }
        if (i < 0) {
            asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
                      "drvGenericSerial: Can't connect to %s: %s\n",
                        tty->serialDeviceName, strerror(errno));
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
            asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
                      "drvGenericSerial: Can't open `%s': %s\n",
                        tty->serialDeviceName, strerror(errno));
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
            asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
                      "drvGenericSerial: Can't set `%s' file flags: %s\n",
                        tty->serialDeviceName, strerror(errno));
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
drvGenericSerialGetPortOption(void *drvPvt, asynUser *pasynUser,
                              const char *key, char *val, int valSize)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int l;

    if (tty->isRemote)
        printf ("Warning -- port option parameters are ignored for remote terminal connections.\n");
    if (epicsStrCaseCmp(key, "baud") == 0) {
        l = epicsSnprintf(val, valSize, "%d", tty->baud);
    }
    else if (epicsStrCaseCmp(key, "bits") == 0) {
        switch (tty->cflag & CSIZE) {
        case CS5: l = epicsSnprintf(val, valSize, "5"); break;
        case CS6: l = epicsSnprintf(val, valSize, "6"); break;
        case CS7: l = epicsSnprintf(val, valSize, "7"); break;
        case CS8: l = epicsSnprintf(val, valSize, "8"); break;
        default:  l = epicsSnprintf(val, valSize, "?"); break;
        }
    }
    else if (epicsStrCaseCmp(key, "parity") == 0) {
        if (tty->cflag & PARENB) {
            if (tty->cflag & PARODD)
                l = epicsSnprintf(val, valSize, "odd");
            else
                l = epicsSnprintf(val, valSize, "even");
        }
        else {
            l = epicsSnprintf(val, valSize, "none");
        }
    }
    else if (epicsStrCaseCmp(key, "stop") == 0) {
        l = epicsSnprintf(val, valSize, "%d",  (tty->cflag & CSTOPB) ? 2 : 1);
    }
    else if (epicsStrCaseCmp(key, "clocal") == 0) {
        l = epicsSnprintf(val, valSize, "%c",  (tty->cflag & CLOCAL) ? 'Y' : 'N');
    }
    else if (epicsStrCaseCmp(key, "crtscts") == 0) {
        char c;
#if defined(CRTSCTS)
            c = (tty->cflag & CRTSCTS) ? 'Y' : 'N';
#else
            c = 'N';
#endif
        l = epicsSnprintf(val, valSize, "%c", c);
    }
    else {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"Unsupported key `%s'\n", key);
        return asynError;
    }
    if (l >= valSize) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"Value buffer for key '%s' is too small.\n", key);
        return asynError;
    }
    return asynSuccess;
}

static asynStatus
drvGenericSerialSetPortOption(void *drvPvt, asynUser *pasynUser,
                              const char *key, const char *val)
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
        case 50:     break;
        case 75:     break;
        case 110:    break;
        case 134:    break;
        case 150:    break;
        case 200:    break;
        case 300:    break;
        case 600:    break;
        case 1200:   break;
        case 1800:   break;
        case 2400:   break;
        case 4800:   break;
        case 9600:   break;
        case 19200:  break;
        case 38400:  break;
        case 57600:  break;
        case 115200: break;
        case 230400: break;
        default:
            asynPrint(tty->pasynUser, ASYN_TRACE_ERROR, "Invalid speed.\n");
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
            asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,"Invalid number of bits.\n");
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
            asynPrint(tty->pasynUser, ASYN_TRACE_ERROR, "Invalid parity.\n");
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
            asynPrint(tty->pasynUser, ASYN_TRACE_ERROR, "Invalid number of stop bits.\n");
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
            asynPrint(tty->pasynUser, ASYN_TRACE_ERROR, "Invalid clocal value.\n");
            return asynError;
        }
    }
    else if (epicsStrCaseCmp(key, "crtscts") == 0) {
        if (epicsStrCaseCmp(val, "Y") == 0) {
#if defined(CRTSCTS)
            tty->cflag |= CRTSCTS;
#else
            asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
                      "Warning -- RTS/CTS flow control is not available on this machine.\n");
#endif
        }
        else if (epicsStrCaseCmp(val, "N") == 0) {
#if defined(CRTSCTS)
            tty->cflag &= ~CRTSCTS;
#endif
        }
        else {
            asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
                      "Invalid crtscts value.\n");
            return asynError;
        }
    }
    else {
        asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
                  "Unsupported key `%s'\n", key);
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
    wrote = write(tty->fd, (char *)data, numchars);
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
 * to read characters when the EOS is a single character, but there
 * are a couple of reasons why this isn't a good idea.
 *  1) The cooked data inside the kernel is a fixed size.  A long
 *     input message could end up being truncated.
 *  2) If the EOS character were changed there could be problem
 *     with losing characters already on the cooked queue.
 */
static int
drvGenericSerialRead(void *drvPvt, asynUser *pasynUser, char *data, int maxchars)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int thisRead;
    int nRead = 0;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
               "drvGenericSerial: %s read.\n", tty->serialDeviceName);
    if (tty->fd < 0) {
      if (tty->openOnlyOnDisconnect) return asynError;
      if (openConnection(tty) != asynSuccess) return asynError;
    }
    if (maxchars <= 0)
        return 0;
    for (;;) {
        while ((tty->inBufferTail != tty->inBufferHead)) {
            char c = *data++ = tty->inBuffer[tty->inBufferTail++];
            if (tty->inBufferTail == INBUFFER_SIZE)
                tty->inBufferTail = 0;
            nRead++;
            tty->nRead++;
            if (tty->eoslen != 0) {
                if (c == tty->eos[tty->eosMatch]) {
                    if (++tty->eosMatch == tty->eoslen) {
                        tty->eosMatch = 0;
                        return nRead;
                    }
                }
                else {
                    /*
                     * Resynchronize the EOS search.  Since the driver
                     * allows a maximum two-character EOS it doesn't
                     * have to worry about cases like:
                     *    End-of-string is "eeef"
                     *    Input stream so far is "eeeeeeeee"
                     */
                    if (c == tty->eos[0])
                        tty->eosMatch = 1;
                    else
                        tty->eosMatch = 0;
                }
            }
            if (nRead >= maxchars)
                return nRead;
        }
        if (tty->inBufferHead >= tty->inBufferTail)
            thisRead = INBUFFER_SIZE - tty->inBufferHead;
        else
            thisRead = tty->inBufferTail;
        epicsTimerStartDelay(tty->timer, pasynUser->timeout);
        thisRead = read(tty->fd, tty->inBuffer + tty->inBufferHead, thisRead);
        epicsTimerCancel(tty->timer);
        if (epicsInterruptibleSyscallWasInterrupted(tty->interruptibleSyscallContext)) {
            reportFailure(tty, "drvGenericSerialRead", "timeout");
            break;
        }
        if (thisRead < 0) {
            reportFailure(tty, "drvGenericSerialRead", strerror(errno));
            break;
        }
        if (thisRead == 0) {
            reportFailure(tty, "drvGenericSerialRead", "unexpected EOF");
            break;
        }
        asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER,
                    tty->inBuffer + tty->inBufferHead, thisRead,
                   "drvGenericSerialRead %d ", thisRead);
        tty->inBufferHead += thisRead;
        if (tty->inBufferHead == INBUFFER_SIZE)
            tty->inBufferHead = 0;
    }
    closeConnection(tty);
    return -1;
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
    tty->inBufferHead = tty->inBufferTail = 0;
    tty->eosMatch = 0;
    return asynSuccess;
}

/*
 * Set the end-of-string message
 */
static asynStatus
drvGenericSerialSetEos(void *drvPvt,asynUser *pasynUser,const char *eos,int eoslen)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    asynPrintIO(pasynUser, ASYN_TRACE_FLOW, eos, eoslen,
            "drvGenericSerialSetEos %d: ", eoslen);
    switch (eoslen) {
    default:
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
           "%s drvGenericSerialSetEos illegal eoslen %d\n",
            tty->serialDeviceName,eoslen);
        return asynError;
    case 2: tty->eos[1] = eos[1]; /* fall through to case 1 */
    case 1: tty->eos[0] = eos[0]; break;
    case 0: break;
    }
    tty->eoslen = eoslen;
    tty->eosMatch = 0;
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
    drvGenericSerialSetPortOption,
    drvGenericSerialGetPortOption
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
