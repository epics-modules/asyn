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
 * $Id: drvGenericSerial.c,v 1.37 2004-04-01 00:54:29 norume Exp $
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
#include <drvGenericSerial.h>

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
    char              *portName;
    int                fd;
    int                isRemote;
    char               eos[2];
    int                eoslen;
    char               inBuffer[INBUFFER_SIZE];
    unsigned int       inBufferHead;
    unsigned int       inBufferTail;
    int                eosMatch;
    osiSockAddr        farAddr;
    epicsTimerId       timer;
    epicsInterruptibleSyscallContext *interruptibleSyscallContext;
    unsigned long      nRead;
    unsigned long      nWritten;
    int                baud;
    int                cflag;
    asynInterface      common;
    asynInterface      octet;
} ttyController_t;

typedef struct serialBase {
    epicsTimerQueueId timerQueue;
}serialBase;
static serialBase *pserialBase = 0;
static void serialBaseInit(void)
{
    if(pserialBase) return;
    pserialBase = callocMustSucceed(1,sizeof(serialBase),"serialBaseInit");
    pserialBase->timerQueue = epicsTimerQueueAllocate(
        1,epicsThreadPriorityScanLow);
}
/*
 * Report link parameters
 */
static void
drvGenericSerialReport(void *drvPvt, FILE *fp, int details)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    fprintf(fp, "Serial line %s: %sonnected\n",
        tty->serialDeviceName,
        tty->fd >= 0 ? "C" : "Disc");
    if (details >= 1) {
        fprintf(fp, "                    fd: %d\n", tty->fd);
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
    asynUser *pasynUser = tty->pasynUser;
    if (tty->fd >= 0) {
        if (!epicsInterruptibleSyscallWasClosed(tty->interruptibleSyscallContext))
            close(tty->fd);
        tty->fd = -1;
        pasynManager->exceptionDisconnect(pasynUser);
    }
}

/*
 * Unblock the I/O operation
 */
static void
timeoutHandler(void *p)
{
    ttyController_t *tty = (ttyController_t *)p;

    asynPrint(tty->pasynUser, ASYN_TRACE_FLOW,
                                       "%s timeout.\n", tty->serialDeviceName);
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
    asynUser *pasynUser = tty->pasynUser;

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
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                            "Invalid speed.");
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
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                  "Can't set \"%s\" attributes: %s",
                                       tty->serialDeviceName, strerror(errno));
        return asynError;
    }
    return asynSuccess;
#elif defined(vxWorks)
    if (ioctl(tty->fd, SIO_HW_OPTS_SET, tty->cflag) < 0)
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "Warning: `%s' does not support SIO_HW_OPTS_SET.\n",
                                                        tty->serialDeviceName);
    return asynSuccess;
#else
    asynPrint(pasynUser, ASYN_TRACE_ERROR,
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
              "Don't know how to set serial port mode on this machine.\n");
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
    asynUser *pasynUser = tty->pasynUser;

    /*
     * Sanity check
     */
    assert(tty);
    if (tty->fd >= 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                              "%s: Link already open!", tty->serialDeviceName);
        return asynError;
    }

    if (tty->interruptibleSyscallContext == NULL)
        tty->interruptibleSyscallContext = epicsInterruptibleSyscallMustCreate("drvGenericSerial");

    /*
     * Create the remote or local connection
     */
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                              "Open connection to %s\n", tty->serialDeviceName);
    if (tty->isRemote) {
        /*
         * Create the socket
         */
        if ((tty->fd = epicsSocketCreate(PF_INET, SOCK_STREAM, 0)) < 0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                  "Can't create socket: %s", strerror(errno));
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
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "Timed out attempting to connect to %s",
                                                        tty->serialDeviceName);
            if (!epicsInterruptibleSyscallWasClosed(tty->interruptibleSyscallContext))
                close(tty->fd);
            tty->fd = -1;
            return asynError;
        }
        if (i < 0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                  "Can't connect to %s: %s",
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
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "Can't open %s: %s\n",
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
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "Can't set %s file flags: %s",
                                        tty->serialDeviceName, strerror(errno));
            close(tty->fd);
            tty->fd = -1;
            return asynError;
        }
#endif
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                          "Opened connection to %s\n", tty->serialDeviceName);
    pasynManager->exceptionConnect(pasynUser);
    return asynSuccess;
}

/*
 * Create a link
 */
static asynStatus
drvGenericSerialConnect(void *drvPvt, asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    asynStatus     status;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                                        "%s connect\n", tty->serialDeviceName);
    status = openConnection(tty);
    return status;
}

static asynStatus
drvGenericSerialDisconnect(void *drvPvt, asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                                    "%s disconnect\n", tty->serialDeviceName);
    if (tty->timer)
        epicsTimerCancel(tty->timer);
    closeConnection(tty);
    return asynSuccess;
}

static asynStatus
drvGenericSerialGetPortOption(void *drvPvt, asynUser *pasynUser,
                              const char *key, char *val, int valSize)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int l;

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
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                "Unsupported key \"%s\"", key);
        return asynError;
    }
    if (l >= valSize) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                            "Value buffer for key '%s' is too small.", key);
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
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"Warning -- port options are ignored for remote terminal connections.\n");
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
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                            "Invalid speed.");
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
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                    "Invalid number of bits.");
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
        else if (epicsStrCaseCmp(val, "odd") == 0) {
            tty->cflag |= PARENB;
            tty->cflag |= PARODD;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                            "Invalid parity.");
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
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                "Invalid number of stop bits.");
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
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                    "Invalid clocal value.");
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
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                      "Invalid crtscts value.");
            return asynError;
        }
    }
    else {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                "Unsupported key \"%s\"", key);
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
    double timeout = pasynUser->timeout;
    int wrote;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                                       "%s write.\n", tty->serialDeviceName);
    if (tty->fd < 0) return -1;
    asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, data, numchars,
                               "%s write %d ", tty->serialDeviceName, numchars);
    if (timeout >= 0)
        epicsTimerStartDelay(tty->timer, timeout);
    wrote = write(tty->fd, (char *)data, numchars);
    if (timeout >= 0)
        epicsTimerCancel(tty->timer);
    if (epicsInterruptibleSyscallWasInterrupted(tty->interruptibleSyscallContext)) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                    "%s write timeout", tty->serialDeviceName);
        closeConnection(tty);
        return -1;
    }
    tty->nWritten += wrote;
    if (wrote != numchars) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "%s write error: %s",
                                        tty->serialDeviceName, strerror(errno));
        closeConnection(tty);
    }
    return wrote;
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
    double timeout = pasynUser->timeout;
    int didTimeout = 0;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
               "%s read.\n", tty->serialDeviceName);
    if (tty->fd < 0) return asynError;
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
        if (didTimeout) {
            if (nRead)
                return nRead;
            break;
        }
        if (tty->inBufferHead >= tty->inBufferTail)
            thisRead = INBUFFER_SIZE - tty->inBufferHead;
        else
            thisRead = tty->inBufferTail;
        if (timeout >= 0)
            epicsTimerStartDelay(tty->timer, timeout);
        thisRead = read(tty->fd, tty->inBuffer + tty->inBufferHead, thisRead);
        if (timeout >= 0)
            epicsTimerCancel(tty->timer);
        if (thisRead > 0) {
            asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER,
                        tty->inBuffer + tty->inBufferHead, thisRead,
                       "%s read %d ", tty->serialDeviceName, thisRead);
            tty->inBufferHead += thisRead;
            if (tty->inBufferHead == INBUFFER_SIZE)
                tty->inBufferHead = 0;
        }
        /*
         * No 'else' in front of this test since it's possible that the
         * timer fired after the read returned but before the timer cancel.
         */
        if (epicsInterruptibleSyscallWasInterrupted(tty->interruptibleSyscallContext)) {
            if (epicsInterruptibleSyscallWasClosed(tty->interruptibleSyscallContext))
                closeConnection(tty);
            asynPrint(pasynUser, ASYN_TRACE_FLOW,
                                    "%s timed out.\n", tty->serialDeviceName);
            didTimeout = 1;
        }
        else if (thisRead < 0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "%s read error: %s",
                                        tty->serialDeviceName, strerror(errno));
            break;
        }
        else if (thisRead == 0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                    "%s unexpected EOF", tty->serialDeviceName);
            break;
        }
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
            "%s flush\n", tty->serialDeviceName);
    if (tty->fd >= 0) {
        if (tty->isRemote) {
            /*
             * Toss characters until there are none left
             */
#ifdef vxWorks
            int flag = 1;
            if (ioctl(tty->fd, FIONBIO, &flag) >= 0) {
#else
            if (fcntl(tty->fd, F_SETFL, O_NONBLOCK) >= 0) {
#endif
                char cbuf[512];
                while (read(tty->fd, cbuf, sizeof cbuf) > 0)
                    continue;
#ifdef vxWorks
                flag = 0;
                ioctl(tty->fd, FIONBIO, &flag);
#else
                fcntl(tty->fd, F_SETFL, 0);
#endif
            }
        }
        else {
#ifdef vxWorks
            ioctl(tty->fd, FIOFLUSH, 0);
#else
            tcflush(tty->fd, TCIOFLUSH);
#endif
        }
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
            "%s set EOS %d: ", tty->serialDeviceName, eoslen);
    switch (eoslen) {
    default:
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                        "%s illegal eoslen %d", tty->serialDeviceName,eoslen);
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
 * Get the end-of-string message
 */
static asynStatus
drvGenericSerialGetEos(void *drvPvt,asynUser *pasynUser,char *eos,
    int eossize, int *eoslen)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    if(tty->eoslen>eossize) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "%s eossize %d < tty->eoslen %d",
                                tty->serialDeviceName,eossize,tty->eoslen);
                            *eoslen = 0;
        return(asynError);
    }
    switch (tty->eoslen) {
    default:
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s illegal tty->eoslen %d", tty->serialDeviceName,tty->eoslen);
        return asynError;
    case 2: eos[1] = tty->eos[1]; /* fall through to case 1 */
    case 1: eos[0] = tty->eos[0]; break;
    case 0: break;
    }
    *eoslen = tty->eoslen;
    asynPrintIO(pasynUser, ASYN_TRACE_FLOW, eos, *eoslen,
            "%s get EOS %d: ", tty->serialDeviceName, eoslen);
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
        free(tty->portName);
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
    drvGenericSerialSetEos,
    drvGenericSerialGetEos
};

/*
 * Configure and register a generic serial device
 */
int
drvGenericSerialConfigure(char *portName,
                     char *ttyName,
                     unsigned int priority,
                     int noAutoConnect)
{
    ttyController_t *tty;
    asynInterface *pasynInterface;
    char *cp;
    asynStatus status;

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

    if(!pserialBase) serialBaseInit();
    /*
     * Create a driver
     */
    tty = (ttyController_t *)callocMustSucceed(1, sizeof *tty, "drvGenericSerialConfigure()");
    /*
     * Create timeout mechanism
     */
     tty->timer = epicsTimerQueueCreateTimer(
         pserialBase->timerQueue, timeoutHandler, tty);
     if(!tty->timer) {
        errlogPrintf("drvGenericSerialConfigure: Can't create timer.\n");
        return -1;
    }
    tty->fd = -1;
    tty->serialDeviceName = epicsStrDup(ttyName);
    tty->portName = epicsStrDup(portName);
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


    /*
     *  Link with higher level routines
     */
    pasynInterface = (asynInterface *)callocMustSucceed(2, sizeof *pasynInterface, "drvGenericSerialConfigure");
    tty->common.interfaceType = asynCommonType;
    tty->common.pinterface  = (void *)&drvGenericSerialAsynCommon;
    tty->common.drvPvt = tty;
    tty->octet.interfaceType = asynOctetType;
    tty->octet.pinterface  = (void *)&drvGenericSerialAsynOctet;
    tty->octet.drvPvt = tty;
    if (pasynManager->registerPort(tty->portName,
                                   0, /*not multiDevice*/
                                   !noAutoConnect,
                                   priority,
                                   0) != asynSuccess) {
        errlogPrintf("drvGenericSerialConfigure: Can't register myself.\n");
        ttyCleanup(tty);
        return -1;
    }
    if(pasynManager->registerInterface(tty->portName,&tty->common)!= asynSuccess) {
        errlogPrintf("drvGenericSerialConfigure: Can't register common.\n");
        ttyCleanup(tty);
        return -1;
    }
    if(pasynManager->registerInterface(tty->portName,&tty->octet)!= asynSuccess) {
        errlogPrintf("drvGenericSerialConfigure: Can't register octet.\n");
        ttyCleanup(tty);
        return -1;
    }
    tty->pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(tty->pasynUser,tty->portName,-1);
    if(status!=asynSuccess)
        printf("connectDevice failed %s\n",tty->pasynUser->errorMessage);

    return 0;
}

/*
 * IOC shell command registration
 */
#include <iocsh.h>
static const iocshArg drvGenericSerialConfigureArg0 = { "port name",iocshArgString};
static const iocshArg drvGenericSerialConfigureArg1 = { "tty name",iocshArgString};
static const iocshArg drvGenericSerialConfigureArg2 = { "priority",iocshArgInt};
static const iocshArg drvGenericSerialConfigureArg3 = { "disable auto-connect",iocshArgInt};
static const iocshArg *drvGenericSerialConfigureArgs[] = {
    &drvGenericSerialConfigureArg0, &drvGenericSerialConfigureArg1,
    &drvGenericSerialConfigureArg2, &drvGenericSerialConfigureArg3};
static const iocshFuncDef drvGenericSerialConfigureFuncDef =
                      {"drvGenericSerialConfigure",4,drvGenericSerialConfigureArgs};
static void drvGenericSerialConfigureCallFunc(const iocshArgBuf *args)
{
    drvGenericSerialConfigure(args[0].sval, args[1].sval, args[2].ival,
                                                                args[3].ival);
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