/**********************************************************************
* Asyn device support using local serial interface                    *
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
 * $Id: drvAsynSerialPort.c,v 1.11 2004-05-10 18:30:57 norume Exp $
 */

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cantProceed.h>
#include <errlog.h>
#include <iocsh.h>
#include <epicsAssert.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <epicsThread.h>
#include <epicsTime.h>
#include <epicsTimer.h>
#include <osiUnistd.h>
#include <epicsExport.h>
#include <asynDriver.h>
#include <asynInterposeEos.h>
#include <drvAsynSerialPort.h>

#if !defined(vxWorks) && !defined(__rtems__)
# define USE_POLL
# include <sys/poll.h>
#endif

#ifdef vxWorks
# include <tyLib.h>
# include <ioLib.h>
# include <sioLib.h>
# define CSTOPB STOPB
#else
# define USE_TERMIOS
# include <termios.h>
#endif

#define CANCEL_CHECK_INTERVAL 5.0 /* Interval between checks for I/O cancel */
#define CONSECUTIVE_READ_TIMEOUT_LIMIT  5   /* Disconnect after this many */

#if !defined(USE_TERMIOS)
/*
 * Fake termios structure
 */
struct termios {
    int c_cflag;
};
#endif

/*
 * This structure holds the hardware-specific information for a single
 * asyn link.  There is one for each serial line.
 */
typedef struct {
    asynUser          *pasynUser;
    char              *serialDeviceName;
    char              *portName;
    int                fd;
    unsigned long      nRead;
    unsigned long      nWritten;
    int                baud;
    struct termios     termios;
    int                consecutiveReadTimeouts;
    double             readTimeout;
    int                readPollmsec;
    double             writeTimeout;
    int                writePollmsec;
    epicsTimerId       timer;
    int                timeoutFlag;
    int                cancelFlag;
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
drvAsynSerialPortReport(void *drvPvt, FILE *fp, int details)
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
 * Close a connection
 */
static void
closeConnection(ttyController_t *tty)
{
    asynUser *pasynUser = tty->pasynUser;
    if (tty->fd >= 0) {
        asynPrint(pasynUser, ASYN_TRACE_FLOW,
                           "Close %s connection.\n", tty->serialDeviceName);
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
                               "%s timeout handler.\n", tty->serialDeviceName);
    tty->timeoutFlag = 1;
#ifdef vxWorks
    ioctl(tty->fd, FIOCANCEL, NULL);
#endif
#ifdef USE_TERMIOS
    tcflush(tty->fd, TCOFLUSH);
#endif
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

#if defined(USE_TERMIOS)
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
    tty->termios.c_iflag = IGNBRK | IGNPAR;
    tty->termios.c_oflag = 0;
    tty->termios.c_lflag = 0;
    tty->termios.c_cc[VMIN] = 0;
    tty->termios.c_cc[VTIME] = 0;
    cfsetispeed(&tty->termios,baudCode);
    cfsetospeed(&tty->termios,baudCode);
    tty->readPollmsec = -1;
    if (tcsetattr(tty->fd, TCSADRAIN, &tty->termios) < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                  "Can't set \"%s\" attributes: %s",
                                       tty->serialDeviceName, strerror(errno));
        return asynError;
    }
    return asynSuccess;
#elif defined(vxWorks)
    if (ioctl(tty->fd, SIO_HW_OPTS_SET, tty->termios.c_cflag) < 0)
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "Warning: `%s' does not support SIO_HW_OPTS_SET.\n",
                                                        tty->serialDeviceName);
    return asynSuccess;
#else
    asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "Warning: No way to set serial port mode on this machine.\n");
    return asynSuccess;
#endif
}

/*
 * Set serial line speed
 */
static asynStatus
setBaud (ttyController_t *tty)
{
#if defined(USE_TERMIOS)
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
              "Warning: No way to set serial port mode on this machine.\n");
    return asynSuccess; 
#endif
}


/*
 * Create a link
 */
static asynStatus
drvAsynSerialPortConnect(void *drvPvt, asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    /*
     * Sanity check
     */
    assert(tty);
    if (tty->fd >= 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                              "%s: Link already open!", tty->serialDeviceName);
        return asynError;
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                              "Open connection to %s\n", tty->serialDeviceName);

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
    if ((setBaud(tty) != asynSuccess)
     || (setMode(tty) != asynSuccess)) {
        close(tty->fd);
        tty->fd = -1;
        return asynError;
    }
   
    /*
     * Turn off non-blocking mode for systems without poll()
     */
#if !defined(USE_POLL) && !defined(vxWorks)
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

    tty->readPollmsec = -1;
    tty->writePollmsec = -1;
    tty->consecutiveReadTimeouts = 0;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                          "Opened connection to %s\n", tty->serialDeviceName);
    pasynManager->exceptionConnect(pasynUser);
    return asynSuccess;
}

static asynStatus
drvAsynSerialPortDisconnect(void *drvPvt, asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                                    "%s disconnect\n", tty->serialDeviceName);
    epicsTimerCancel(tty->timer);
    closeConnection(tty);
    return asynSuccess;
}

static asynStatus
drvAsynSerialPortGetPortOption(void *drvPvt, asynUser *pasynUser,
                              const char *key, char *val, int valSize)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int l;

    if (epicsStrCaseCmp(key, "baud") == 0) {
        l = epicsSnprintf(val, valSize, "%d", tty->baud);
    }
    else if (epicsStrCaseCmp(key, "bits") == 0) {
        switch (tty->termios.c_cflag & CSIZE) {
        case CS5: l = epicsSnprintf(val, valSize, "5"); break;
        case CS6: l = epicsSnprintf(val, valSize, "6"); break;
        case CS7: l = epicsSnprintf(val, valSize, "7"); break;
        case CS8: l = epicsSnprintf(val, valSize, "8"); break;
        default:  l = epicsSnprintf(val, valSize, "?"); break;
        }
    }
    else if (epicsStrCaseCmp(key, "parity") == 0) {
        if (tty->termios.c_cflag & PARENB) {
            if (tty->termios.c_cflag & PARODD)
                l = epicsSnprintf(val, valSize, "odd");
            else
                l = epicsSnprintf(val, valSize, "even");
        }
        else {
            l = epicsSnprintf(val, valSize, "none");
        }
    }
    else if (epicsStrCaseCmp(key, "stop") == 0) {
        l = epicsSnprintf(val, valSize, "%d",  (tty->termios.c_cflag & CSTOPB) ? 2 : 1);
    }
    else if (epicsStrCaseCmp(key, "clocal") == 0) {
        l = epicsSnprintf(val, valSize, "%c",  (tty->termios.c_cflag & CLOCAL) ? 'Y' : 'N');
    }
    else if (epicsStrCaseCmp(key, "crtscts") == 0) {
        char c;
#if defined(CRTSCTS)
            c = (tty->termios.c_cflag & CRTSCTS) ? 'Y' : 'N';
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
drvAsynSerialPortSetPortOption(void *drvPvt, asynUser *pasynUser,
                              const char *key, const char *val)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int baud = 0;

    assert(tty);
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
            tty->termios.c_cflag = (tty->termios.c_cflag & ~CSIZE) | CS5;
        }
        else if (epicsStrCaseCmp(val, "6") == 0) {
            tty->termios.c_cflag = (tty->termios.c_cflag & ~CSIZE) | CS6;
        }
        else if (epicsStrCaseCmp(val, "7") == 0) {
            tty->termios.c_cflag = (tty->termios.c_cflag & ~CSIZE) | CS7;
        }
        else if (epicsStrCaseCmp(val, "8") == 0) {
            tty->termios.c_cflag = (tty->termios.c_cflag & ~CSIZE) | CS8;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                    "Invalid number of bits.");
            return asynError;
        }
    }
    else if (epicsStrCaseCmp(key, "parity") == 0) {
        if (epicsStrCaseCmp(val, "none") == 0) {
            tty->termios.c_cflag &= ~PARENB;
        }
        else if (epicsStrCaseCmp(val, "even") == 0) {
            tty->termios.c_cflag |= PARENB;
            tty->termios.c_cflag &= ~PARODD;
        }
        else if (epicsStrCaseCmp(val, "odd") == 0) {
            tty->termios.c_cflag |= PARENB;
            tty->termios.c_cflag |= PARODD;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                            "Invalid parity.");
            return asynError;
        }
    }
    else if (epicsStrCaseCmp(key, "stop") == 0) {
        if (epicsStrCaseCmp(val, "1") == 0) {
            tty->termios.c_cflag &= ~CSTOPB;
        }
        else if (epicsStrCaseCmp(val, "2") == 0) {
            tty->termios.c_cflag |= CSTOPB;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                "Invalid number of stop bits.");
            return asynError;
        }
    }
    else if (epicsStrCaseCmp(key, "clocal") == 0) {
        if (epicsStrCaseCmp(val, "Y") == 0) {
            tty->termios.c_cflag |= CLOCAL;
        }
        else if (epicsStrCaseCmp(val, "N") == 0) {
            tty->termios.c_cflag &= ~CLOCAL;
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
            tty->termios.c_cflag |= CRTSCTS;
#else
            asynPrint(tty->pasynUser, ASYN_TRACE_ERROR,
                      "Warning -- RTS/CTS flow control is not available on this machine.\n");
#endif
        }
        else if (epicsStrCaseCmp(val, "N") == 0) {
#if defined(CRTSCTS)
            tty->termios.c_cflag &= ~CRTSCTS;
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
static asynStatus drvAsynSerialPortWrite(void *drvPvt, asynUser *pasynUser,
    const char *data, int numchars,int *nbytesTransfered)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int thisWrite;
    int nleft = numchars;
    int timerStarted = 0;
    asynStatus status = asynSuccess;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                            "%s write.\n", tty->serialDeviceName);
    asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, data, numchars,
                            "%s write %d ", tty->serialDeviceName, numchars);
    if (tty->fd < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "%s disconnected:", tty->serialDeviceName);
        return asynError;
    }
    if ((tty->writePollmsec < 0) || (pasynUser->timeout != tty->writeTimeout)) {
        tty->writeTimeout = pasynUser->timeout;
        if (tty->writeTimeout == 0) {
            tty->writePollmsec = 0;
        }
        else if ((tty->writeTimeout > 0) && (tty->writeTimeout <= CANCEL_CHECK_INTERVAL)) {
            tty->writePollmsec = tty->writeTimeout * 1000.0;
            if (tty->writePollmsec == 0)
                tty->writePollmsec = 1;
        }
        else {
            tty->writePollmsec = CANCEL_CHECK_INTERVAL * 1000.0;
        }
    }
    tty->cancelFlag = 0;
    tty->timeoutFlag = 0;
    nleft = numchars;
#ifdef vxWorks
    if (tty->writeTimeout >= 0)
#else
    if (tty->writeTimeout > 0)
#endif
        {
        epicsTimerStartDelay(tty->timer, tty->writeTimeout);
        timerStarted = 1;
        }
    for (;;) {
#ifdef USE_POLL
        {
        struct pollfd pollfd;
        pollfd.fd = tty->fd;
        pollfd.events = POLLOUT;
        poll(&pollfd, 1, tty->writePollmsec);
        }
#endif
        thisWrite = write(tty->fd, (char *)data, nleft);
        if (thisWrite > 0) {
            tty->nWritten += thisWrite;
            nleft -= thisWrite;
            if (nleft == 0)
                break;
            data += thisWrite;
        }
        if (tty->cancelFlag) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                    "%s I/O cancelled", tty->serialDeviceName);
            status = asynError;
            break;
        }
        if (tty->timeoutFlag || (tty->writePollmsec == 0)) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                    "%s timeout", tty->serialDeviceName);
            status = asynError;
            break;
        }
        if ((thisWrite < 0) && (errno != EWOULDBLOCK)
                            && (errno != EINTR)
                            && (errno != EAGAIN)) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "%s write error: %s",
                                        tty->serialDeviceName, strerror(errno));
            closeConnection(tty);
            status = asynError;
            break;
        }
    }
    if (timerStarted) epicsTimerCancel(tty->timer);
    *nbytesTransfered = numchars - nleft;
    return status;
}

/*
 * Read from the serial line
 */
static asynStatus drvAsynSerialPortRead(void *drvPvt, asynUser *pasynUser,
    char *data, int maxchars,int *nbytesTransfered)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int thisRead;
    int nRead = 0;
    int timerStarted = 0;
    asynStatus status = asynSuccess;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
               "%s read.\n", tty->serialDeviceName);
    if (tty->fd < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "%s disconnected:", tty->serialDeviceName);
        return asynError;
    }
    if (maxchars <= 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s maxchars %d Why <=0?\n",tty->serialDeviceName,maxchars);
        return asynError;
    }
    if ((tty->readPollmsec < 0) || (pasynUser->timeout != tty->readTimeout)) {
        tty->readTimeout = pasynUser->timeout;
        if (tty->readTimeout == 0) {
            tty->readPollmsec = 0;
        }
        else if ((tty->readTimeout > 0) && (tty->readTimeout <= CANCEL_CHECK_INTERVAL)) {
            tty->readPollmsec = tty->readTimeout * 1000.0;
            if (tty->readPollmsec == 0)
                tty->readPollmsec = 1;
        }
        else {
            tty->readPollmsec = CANCEL_CHECK_INTERVAL * 1000.0;
        }
#ifdef __rtems__
        /*
         * RTEMS has neither poll() nor ioctl(FIOCANCEL) and so must rely
         * on termios timeouts.
         */
        tty->termios.c_cc[VTIME] = (tty->readPollmsec + 99) / 100;
        if (tcsetattr(tty->fd, TCSADRAIN, &tty->termios) < 0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                              "Can't set \"%s\" c_cc[VTIME]: %s",
                                       tty->serialDeviceName, strerror(errno));
            return asynError;
        }
#endif
    }
    tty->cancelFlag = 0;
    tty->timeoutFlag = 0;
    for (;;) {
#ifdef vxWorks
        /*
         * vxWorks has neither poll() nor termios but does have the
         * ability to cancel an operation in progress.  If the read
         * timeout is zero we have to check for characters explicitly
         * since we don't want to start a timer with 0 delay.
         */
        if (tty->readTimeout == 0) {
            int nready;
            ioctl(tty->fd, FIONREAD, &nready);
            if (nready == 0) {
                tty->timeoutFlag = 1;
                break;
            }
        }
#endif
        if (!timerStarted && (tty->readTimeout > 0)) {
            epicsTimerStartDelay(tty->timer, tty->readTimeout);
            timerStarted = 1;
        }
#ifdef USE_POLL
        {
        struct pollfd pollfd;
        pollfd.fd = tty->fd;
        pollfd.events = POLLIN;
        poll(&pollfd, 1, tty->readPollmsec);
        }
#endif
        thisRead = read(tty->fd, data, maxchars);
        if (thisRead > 0) {
            asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, data, thisRead,
                       "%s read %d ", tty->serialDeviceName, thisRead);
            tty->consecutiveReadTimeouts = 0;
            nRead = thisRead;
            break;
        }
        else {
            if ((thisRead < 0) && (errno != EWOULDBLOCK)
                               && (errno != EINTR)
                               && (errno != EAGAIN)) {
                epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "%s read error: %s",
                                        tty->serialDeviceName, strerror(errno));
                closeConnection(tty);
                status = asynError;
                break;
            }
            if (tty->readTimeout == 0)
                tty->timeoutFlag = 1;
        }
        if (tty->cancelFlag) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                    "%s I/O cancelled", tty->serialDeviceName);
            status = asynError;
            break;
        }
        if (tty->timeoutFlag)
            break;
    }
    if (timerStarted) epicsTimerCancel(tty->timer);
    if ((nRead == 0) && (status == asynSuccess) && tty->timeoutFlag) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                        "%s timeout", tty->serialDeviceName);
        if (++tty->consecutiveReadTimeouts >= CONSECUTIVE_READ_TIMEOUT_LIMIT)
            closeConnection(tty);
        status = asynTimeout;
    }
    *nbytesTransfered = nRead;
    return status;
}

/*
 * Flush pending input
 */
static asynStatus
drvAsynSerialPortFlush(void *drvPvt,asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s flush\n", tty->serialDeviceName);
    if (tty->fd >= 0) {
#ifdef vxWorks
        ioctl(tty->fd, FIOFLUSH, 0);
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
drvAsynSerialPortSetEos(void *drvPvt,asynUser *pasynUser,const char *eos,int eoslen)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    asynPrintIO(pasynUser, ASYN_TRACE_FLOW, eos, eoslen,
            "%s set EOS %d: ", tty->serialDeviceName, eoslen);
    return asynError;
}

/*
 * Get the end-of-string message
 */
static asynStatus
drvAsynSerialPortGetEos(void *drvPvt,asynUser *pasynUser,char *eos,
    int eossize, int *eoslen)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
            "%s get EOS\n", tty->serialDeviceName);
    return asynError;
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
static const struct asynCommon drvAsynSerialPortAsynCommon = {
    drvAsynSerialPortReport,
    drvAsynSerialPortConnect,
    drvAsynSerialPortDisconnect,
    drvAsynSerialPortSetPortOption,
    drvAsynSerialPortGetPortOption
};

/*
 * asynOctet methods
 */
static const struct asynOctet drvAsynSerialPortAsynOctet = {
    drvAsynSerialPortRead,
    drvAsynSerialPortWrite,
    drvAsynSerialPortFlush,
    drvAsynSerialPortSetEos,
    drvAsynSerialPortGetEos
};

/*
 * Configure and register a generic serial device
 */
int
drvAsynSerialPortConfigure(char *portName,
                     char *ttyName,
                     unsigned int priority,
                     int noAutoConnect,
                     int noEosProcessing)
{
    ttyController_t *tty;
    asynInterface *pasynInterface;
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
    tty = (ttyController_t *)callocMustSucceed(1, sizeof *tty, "drvAsynSerialPortConfigure()");
    /*
     * Create timeout mechanism
     */
     tty->timer = epicsTimerQueueCreateTimer(
         pserialBase->timerQueue, timeoutHandler, tty);
     if(!tty->timer) {
        errlogPrintf("drvAsynSerialPortConfigure: Can't create timer.\n");
        return -1;
    }
    tty->fd = -1;
    tty->serialDeviceName = epicsStrDup(ttyName);
    tty->portName = epicsStrDup(portName);
    tty->baud = 9600;
    tty->termios.c_cflag = CREAD | CLOCAL | CS8;

    /*
     *  Link with higher level routines
     */
    pasynInterface = (asynInterface *)callocMustSucceed(2, sizeof *pasynInterface, "drvAsynSerialPortConfigure");
    tty->common.interfaceType = asynCommonType;
    tty->common.pinterface  = (void *)&drvAsynSerialPortAsynCommon;
    tty->common.drvPvt = tty;
    tty->octet.interfaceType = asynOctetType;
    tty->octet.pinterface  = (void *)&drvAsynSerialPortAsynOctet;
    tty->octet.drvPvt = tty;
    if (pasynManager->registerPort(tty->portName,
                                   0, /*not multiDevice*/
                                   !noAutoConnect,
                                   priority,
                                   0) != asynSuccess) {
        errlogPrintf("drvAsynSerialPortConfigure: Can't register myself.\n");
        ttyCleanup(tty);
        return -1;
    }
    if(pasynManager->registerInterface(tty->portName,&tty->common)!= asynSuccess) {
        errlogPrintf("drvAsynSerialPortConfigure: Can't register common.\n");
        ttyCleanup(tty);
        return -1;
    }
    if(pasynManager->registerInterface(tty->portName,&tty->octet)!= asynSuccess) {
        errlogPrintf("drvAsynSerialPortConfigure: Can't register octet.\n");
        ttyCleanup(tty);
        return -1;
    }
    tty->pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(tty->pasynUser,tty->portName,-1);
    if(status!=asynSuccess) {
        errlogPrintf("connectDevice failed %s\n",tty->pasynUser->errorMessage);
        ttyCleanup(tty);
        return -1;
    }
    if (!noEosProcessing && (asynInterposeEosConfig(tty->portName, -1) < 0)) {
        ttyCleanup(tty);
        return -1;
    }
    return 0;
}

/*
 * IOC shell command registration
 */
#include <iocsh.h>
static const iocshArg drvAsynSerialPortConfigureArg0 = { "port name",iocshArgString};
static const iocshArg drvAsynSerialPortConfigureArg1 = { "tty name",iocshArgString};
static const iocshArg drvAsynSerialPortConfigureArg2 = { "priority",iocshArgInt};
static const iocshArg drvAsynSerialPortConfigureArg3 = { "disable auto-connect",iocshArgInt};
static const iocshArg drvAsynSerialPortConfigureArg4 = { "disable EOS processing",iocshArgInt};
static const iocshArg *drvAsynSerialPortConfigureArgs[] = {
    &drvAsynSerialPortConfigureArg0, &drvAsynSerialPortConfigureArg1,
    &drvAsynSerialPortConfigureArg2, &drvAsynSerialPortConfigureArg3,
    &drvAsynSerialPortConfigureArg4};
static const iocshFuncDef drvAsynSerialPortConfigureFuncDef =
                      {"drvAsynSerialPortConfigure",5,drvAsynSerialPortConfigureArgs};
static void drvAsynSerialPortConfigureCallFunc(const iocshArgBuf *args)
{
    drvAsynSerialPortConfigure(args[0].sval, args[1].sval, args[2].ival,
                                                args[3].ival, args[4].ival);
}

/*
 * This routine is called before multitasking has started, so there's
 * no race condition in the test/set of firstTime.
 */
static void
drvAsynSerialPortRegisterCommands(void)
{
    static int firstTime = 1;
    if (firstTime) {
        iocshRegister(&drvAsynSerialPortConfigureFuncDef,drvAsynSerialPortConfigureCallFunc);
        firstTime = 0;
    }
}
epicsExportRegistrar(drvAsynSerialPortRegisterCommands);
