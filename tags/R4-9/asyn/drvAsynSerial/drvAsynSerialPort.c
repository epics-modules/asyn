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
 * $Id: drvAsynSerialPort.c,v 1.42 2007-09-20 17:45:25 norume Exp $
 */

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#include <osiUnistd.h>
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
#include "asynDriver.h"
#include "asynOctet.h"
#include "asynOption.h"
#include "asynInterposeEos.h"
#include "drvAsynSerialPort.h"

#ifdef vxWorks
# include <tyLib.h>
# include <ioLib.h>
# include <sioLib.h>
# include <sys/ioctl.h>
# define CSTOPB STOPB
#else
# include <termios.h>
#endif

#ifdef vxWorks
/*
 * Fake termios structure
 */
struct termios {
    int c_cflag;
    int baud;
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
    struct termios     termios;
    double             readTimeout;
    double             writeTimeout;
    epicsTimerId       timer;
    volatile int       timeoutFlag;
    asynInterface      common;
    asynInterface      option;
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
 * Close a connection
 */
static void
closeConnection(asynUser *pasynUser,ttyController_t *tty)
{
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
#ifdef TCOFLUSH
    tcflush(tty->fd, TCOFLUSH);
#endif
#ifdef vxWorks
    ioctl(tty->fd, FIOCANCEL, NULL);
    /*
     * Since it is possible, though unlikely, that we got here before the
     * slow system call actually started, we arrange to poke the thread
     * again in a little while.
     */
    epicsTimerStartDelay(tty->timer, 10.0);
#endif
}

static asynStatus
termiosGet (asynUser *pasynUser,ttyController_t *tty)
{
#ifdef vxWorks
    int baud;
    if(ioctl(tty->fd, SIO_HW_OPTS_GET, (int)&tty->termios.c_cflag) < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "ioctl SIO_HW_OPTS_GET failed %s\n",strerror(errno));
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "%s ioctl SIO_HW_OPTS_GET failed %s\n",
            tty->serialDeviceName,strerror(errno));
        return asynError;
    }
    if(ioctl(tty->fd, SIO_BAUD_GET, (int)&baud) == 0) {
        tty->termios.baud = baud;
    }
#else
    if(tcgetattr(tty->fd,&tty->termios) < 0 ) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "tcgetattr failed %s\n",strerror(errno));
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "%s tcgetattr failed %s\n",
            tty->serialDeviceName,strerror(errno));
        return asynError;
    }
#endif
    return asynSuccess;
}

static asynStatus
termiosInit(asynUser *pasynUser ,ttyController_t *tty)
{

    termiosGet(pasynUser,tty);
#ifndef vxWorks
    tty->termios.c_iflag = IGNBRK | IGNPAR;
    tty->termios.c_oflag = 0;
    tty->termios.c_lflag = 0;
    tty->termios.c_cc[VMIN] = 0;
    tty->termios.c_cc[VTIME] = 0;
    if (tcsetattr(tty->fd, TCSANOW, &tty->termios) < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "tcsetattr failed %s\n",strerror(errno));
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "%s tcsetattr failed %s\n",
            tty->serialDeviceName,strerror(errno));
        return asynError;
    }
#endif
    return asynSuccess;
}

static asynStatus
getBaud(asynUser *pasynUser ,ttyController_t *tty,int *baud)
{
#ifdef vxWorks
    *baud = tty->termios.baud;
#else
    speed_t inBaud,outBaud;

    termiosGet(pasynUser,tty);
    inBaud = cfgetispeed(&tty->termios);
    outBaud = cfgetospeed(&tty->termios);
    if(inBaud!=outBaud) {
       epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
          "cfgetispeed = %d cfgetospeed = %d are not the same\n",
          (int)inBaud,(int)outBaud);
       return asynError;
    }
    switch(inBaud) {
        case B50:     *baud = 50 ;     break;
        case B75:     *baud = 75 ;     break;
        case B110:    *baud = 110 ;    break;
        case B134:    *baud = 134 ;    break;
        case B150:    *baud = 150 ;    break;
        case B200:    *baud = 200 ;    break;
        case B300:    *baud = 300 ;    break;
        case B600:    *baud = 600 ;    break;
        case B1200:   *baud = 1200 ;   break;
        case B1800:   *baud = 1800 ;   break;
        case B2400:   *baud = 2400 ;   break;
        case B4800:   *baud = 4800 ;   break;
        case B9600:   *baud = 9600 ;   break;
        case B19200:  *baud = 19200 ;  break;
#ifdef B28800
        case B28800:  *baud = 28800 ;  break;
#endif
        case B38400:  *baud = 38400 ;  break;
        case B57600:  *baud = 57600 ;  break;
        case B115200: *baud = 115200 ; break;
        case B230400: *baud = 230400 ; break;
        default:
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
              "cfgetispeed returned unknown size");
            return asynError;
    }
#endif
    return asynSuccess;
}
static asynStatus
setBaud(asynUser *pasynUser ,ttyController_t *tty,int baud)
{
#ifdef vxWorks
    int inBaud;
    if ((ioctl(tty->fd, FIOBAUDRATE, baud) < 0)
    && (ioctl(tty->fd, SIO_BAUD_SET, baud) < 0)) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "SIO_BAUD_SET returned %s\n",strerror(errno));
        return asynError;
    }
    if(ioctl(tty->fd, SIO_BAUD_GET, (int)&inBaud) == 0) {
        if(inBaud!=baud) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
               "ioctl SIO_BAUD_GET failed in setBaud %d baud %d\n",
                inBaud,baud);
            return asynError;
        }
    }
    tty->termios.baud = baud;
    return asynSuccess;
#else
    speed_t baudCode;
    int inBaud;
    asynStatus status;

    termiosGet(pasynUser,tty);
    switch(baud) {
        case 50:     baudCode = B50 ;     break;
        case 75:     baudCode = B75 ;     break;
        case 110:    baudCode = B110 ;    break;
        case 134:    baudCode = B134 ;    break;
        case 150:    baudCode = B150 ;    break;
        case 200:    baudCode = B200 ;    break;
        case 300:    baudCode = B300 ;    break;
        case 600:    baudCode = B600 ;    break;
        case 1200:   baudCode = B1200 ;   break;
        case 1800:   baudCode = B1800 ;   break;
        case 2400:   baudCode = B2400 ;   break;
        case 4800:   baudCode = B4800 ;   break;
        case 9600:   baudCode = B9600 ;   break;
        case 19200:  baudCode = B19200 ;  break;
#ifdef B28800
        case 28800:  baudCode = B28800 ;  break;
#endif
        case 38400:  baudCode = B38400 ;  break;
        case 57600:  baudCode = B57600 ;  break;
        case 115200: baudCode = B115200 ; break;
        case 230400: baudCode = B230400 ; break;
        default:
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
              "cfgetispeed returned unknown size");
            return asynError;
    }
    if(cfsetispeed(&tty->termios,baudCode) < 0 ) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "cfsetispeed returned %s\n",strerror(errno));
        return asynError;
    }
    if(cfsetospeed(&tty->termios,baudCode) < 0 ) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "cfsetospeed returned %s\n",strerror(errno));
        return asynError;
    }
    if (tcsetattr(tty->fd, TCSANOW, &tty->termios) < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "tcsetattr failed %s\n",strerror(errno));
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "%s tcsetattr failed %s\n",
            tty->serialDeviceName,strerror(errno));
        return asynError;
    }
    status = getBaud(pasynUser,tty,&inBaud);
    if(status!=asynSuccess) return status;
    if(inBaud!=baud) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "getBaud %d != baud %d\n",inBaud,baud);
        return asynError;
    }
    return asynSuccess;
#endif
}

/*
 * Report link parameters
 */
static void
report(void *drvPvt, FILE *fp, int details)
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
 * Create a link
 */
static asynStatus
connectIt(void *drvPvt, asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    asynStatus status;

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
                            "%s Can't open  %s\n",
                                    tty->serialDeviceName, strerror(errno));
        return asynError;
    }
#if defined(FD_CLOEXEC) && !defined(vxWorks)
    if (fcntl(tty->fd, F_SETFD, FD_CLOEXEC) < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                            "Can't set %s close-on-exec flag: %s\n",
                                    tty->serialDeviceName, strerror(errno));
        close(tty->fd);
        tty->fd = -1;
        return asynError;
    }
#endif
    status = termiosInit(pasynUser,tty);
    if(status!=asynSuccess) {
        close(tty->fd);
        tty->fd = -1;
        return asynError;
    }
   
    /*
     * Turn off non-blocking mode
     */
#ifndef vxWorks
    tcflush(tty->fd, TCIOFLUSH);
    tty->readTimeout = -1e-99;
    tty->writeTimeout = -1e-99;
    if (fcntl(tty->fd, F_SETFL, 0) < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                            "Can't set %s file flags: %s",
                                    tty->serialDeviceName, strerror(errno));
        close(tty->fd);
        tty->fd = -1;
        return asynError;
    }
#endif

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                          "Opened connection to %s\n", tty->serialDeviceName);
    pasynManager->exceptionConnect(pasynUser);
    return asynSuccess;
}

static asynStatus
disconnect(void *drvPvt, asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                                    "%s disconnect\n", tty->serialDeviceName);
    epicsTimerCancel(tty->timer);
    closeConnection(pasynUser,tty);
    return asynSuccess;
}

static asynStatus
getOption(void *drvPvt, asynUser *pasynUser,
                              const char *key, char *val, int valSize)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    asynStatus status;
    int l;

    if (epicsStrCaseCmp(key, "baud") == 0) {
        int baud;
        

        status = getBaud(pasynUser,tty,&baud);
        if(status!=asynSuccess) return status;
        l = epicsSnprintf(val, valSize, "%d", baud);
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
/* vxWorks uses CLOCAL when it should use CRTSCTS */
#ifdef vxWorks
            c = (tty->termios.c_cflag & CLOCAL) ? 'N' : 'Y';
#else
            c = (tty->termios.c_cflag & CRTSCTS) ? 'Y' : 'N';
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
setOption(void *drvPvt, asynUser *pasynUser,
                              const char *key, const char *val)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    asynStatus status;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s setOption key %s val %s\n",tty->portName,key,val);
    if (epicsStrCaseCmp(key, "baud") == 0) {
        int baud;
        if(sscanf(val,"%d",&baud) !=1) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "sscanf failed converting value");
            return asynError;
        }
        status = setBaud(pasynUser,tty,baud);
        return status;
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
/* vxWorks uses CLOCAL when it should use CRTSCTS */
#ifdef vxWorks
            tty->termios.c_cflag &= ~CLOCAL;
#else
            tty->termios.c_cflag |= CRTSCTS;
#endif
        }
        else if (epicsStrCaseCmp(val, "N") == 0) {
#ifdef vxWorks
            tty->termios.c_cflag |= CLOCAL;
#else
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
#ifdef vxWorks
    if(ioctl(tty->fd,SIO_HW_OPTS_SET,tty->termios.c_cflag) < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "ioctl SIO_HW_OPTS_SET failed %s\n",strerror(errno));
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "%s ioctl SIO_HW_OPTS_SET failed %s\n",
            tty->serialDeviceName,strerror(errno));
        return asynError;
    }
#else
    if (tcsetattr(tty->fd, TCSANOW, &tty->termios) < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "tcsetattr failed %s\n",strerror(errno));
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "%s tcsetattr failed %s\n",
            tty->serialDeviceName,strerror(errno));
        return asynError;
    }
#endif
    return asynSuccess;
}

/*
 * Write to the serial line
 */
static asynStatus writeRaw(void *drvPvt, asynUser *pasynUser,
    const char *data, size_t numchars,size_t *nbytesTransfered)
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
                            "%s write %d\n", tty->serialDeviceName, numchars);
    if (tty->fd < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "%s disconnected:", tty->serialDeviceName);
        return asynError;
    }
    if (numchars == 0) {
        *nbytesTransfered = 0;
        return asynSuccess;
    }
    if (tty->writeTimeout != pasynUser->timeout) {
#ifndef vxWorks
        /*
         * Must set flags if we're transitioning
         * between blocking and non-blocking.
         */
        if ((pasynUser->timeout == 0) || (tty->writeTimeout == 0)) {
            int newFlags = (pasynUser->timeout == 0) ? O_NONBLOCK : 0;
            if (fcntl(tty->fd, F_SETFL, newFlags) < 0) {
                epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                            "Can't set %s file flags: %s",
                                    tty->serialDeviceName, strerror(errno));
                return asynError;
            }
        }
#endif
        tty->writeTimeout = pasynUser->timeout;
    }
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
        thisWrite = write(tty->fd, (char *)data, nleft);
        if (thisWrite > 0) {
            tty->nWritten += thisWrite;
            nleft -= thisWrite;
            if (nleft == 0)
                break;
            data += thisWrite;
        }
        if (tty->timeoutFlag || (tty->writeTimeout == 0)) {
            status = asynTimeout;
            break;
        }
        if ((thisWrite < 0) && (errno != EWOULDBLOCK)
                            && (errno != EINTR)
                            && (errno != EAGAIN)) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "%s write error: %s",
                                        tty->serialDeviceName, strerror(errno));
            closeConnection(pasynUser,tty);
            status = asynError;
            break;
        }
    }
    if (timerStarted) epicsTimerCancel(tty->timer);
    *nbytesTransfered = numchars - nleft;
    asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s wrote %d, return %d\n",
                            tty->serialDeviceName, *nbytesTransfered, status);
    return status;
}

/*
 * Read from the serial line
 */
static asynStatus readRaw(void *drvPvt, asynUser *pasynUser,
    char *data, size_t maxchars,size_t *nbytesTransfered,int *gotEom)
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
            "%s maxchars %d Why <=0?\n",tty->serialDeviceName,(int)maxchars);
        return asynError;
    }
    if (tty->readTimeout != pasynUser->timeout) {
#ifndef vxWorks
        /*
         * Must set flags if we're transitioning
         * between blocking and non-blocking.
         */
        if ((pasynUser->timeout == 0) || (tty->readTimeout == 0)) {
            int newFlags = (pasynUser->timeout == 0) ? O_NONBLOCK : 0;
            if (fcntl(tty->fd, F_SETFL, newFlags) < 0) {
                epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                            "Can't set %s file flags: %s",
                                    tty->serialDeviceName, strerror(errno));
                return asynError;
            }
        }
        /*
         * Set TERMIOS timeout
         */
        if (pasynUser->timeout > 0) {
            int t = (pasynUser->timeout * 10) + 1;
            if (t > 255)
                t = 255;
            tty->termios.c_cc[VMIN] = 0;
            tty->termios.c_cc[VTIME] = t;
        }
        else if (pasynUser->timeout == 0) {
            tty->termios.c_cc[VMIN] = 0;
            tty->termios.c_cc[VTIME] = 0;
        }
        else {
            tty->termios.c_cc[VMIN] = 1;
            tty->termios.c_cc[VTIME] = 0;
        }
            
        if (tcsetattr(tty->fd, TCSANOW, &tty->termios) < 0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                              "Can't set \"%s\" c_cc[VTIME]: %s",
                                       tty->serialDeviceName, strerror(errno));
            return asynError;
        }
#endif
        tty->readTimeout = pasynUser->timeout;
    }
    tty->timeoutFlag = 0;
    if (gotEom) *gotEom = 0;
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
            ioctl(tty->fd, FIONREAD, (int)&nready);
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
        thisRead = read(tty->fd, data, maxchars);
        if (thisRead > 0) {
            asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, data, thisRead,
                       "%s read %d\n", tty->serialDeviceName, thisRead);
            nRead = thisRead;
            tty->nRead += thisRead;
            break;
        }
        else {
            if ((thisRead < 0) && (errno != EWOULDBLOCK)
                               && (errno != EINTR)
                               && (errno != EAGAIN)) {
                epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "%s read error: %s",
                                        tty->serialDeviceName, strerror(errno));
                closeConnection(pasynUser,tty);
                status = asynError;
                break;
            }
            if (tty->readTimeout == 0)
                tty->timeoutFlag = 1;
        }
        if (tty->timeoutFlag)
            break;
    }
    if (timerStarted) epicsTimerCancel(tty->timer);
    if (tty->timeoutFlag && (status == asynSuccess))
        status = asynTimeout;
    *nbytesTransfered = nRead;
    /* If there is room add a null byte */
    if (nRead < maxchars)
        data[nRead] = 0;
    else if (gotEom)
        *gotEom = ASYN_EOM_CNT;
    asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s read %d, return %d\n",
                            tty->serialDeviceName, *nbytesTransfered, status);
    return status;
}

/*
 * Flush pending input
 */
static asynStatus
flushIt(void *drvPvt,asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s flush\n", tty->serialDeviceName);
    if (tty->fd >= 0) {
#ifdef vxWorks
        ioctl(tty->fd, FIORFLUSH, 0);
#else
        tcflush(tty->fd, TCIFLUSH);
#endif
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
        free(tty->portName);
        free(tty->serialDeviceName);
        free(tty);
    }
}

/*
 * asynCommon methods
 */
static const struct asynCommon drvAsynSerialPortAsynCommon = {
    report,
    connectIt,
    disconnect
};

/*
 * asynOption methods
 */
static const struct asynOption drvAsynSerialPortAsynOption = {
    setOption,
    getOption
};

/*
 * Configure and register a generic serial device
 */
int
drvAsynSerialPortConfigure(char *portName,
                     char *ttyName,
                     unsigned int priority,
                     int noAutoConnect,
                     int noProcessEos)
{
    ttyController_t *tty;
    asynInterface *pasynInterface;
    asynStatus status;
    int nbytes;
    asynOctet *pasynOctet;


    /*
     * Check arguments
     */
    if (portName == NULL) {
        printf("Port name missing.\n");
        return -1;
    }
    if (ttyName == NULL) {
        printf("TTY name missing.\n");
        return -1;
    }

    if(!pserialBase) serialBaseInit();
    /*
     * Create a driver
     */
    nbytes = sizeof(*tty) + sizeof(asynOctet);
    tty = (ttyController_t *)callocMustSucceed(1, nbytes,
         "drvAsynSerialPortConfigure()");
    pasynOctet = (asynOctet *)(tty +1);
    /*
     * Create timeout mechanism
     */
     tty->timer = epicsTimerQueueCreateTimer(
         pserialBase->timerQueue, timeoutHandler, tty);
     if(!tty->timer) {
        printf("drvAsynSerialPortConfigure: Can't create timer.\n");
        return -1;
    }
    tty->fd = -1;
    tty->serialDeviceName = epicsStrDup(ttyName);
    tty->portName = epicsStrDup(portName);

    /*
     *  Link with higher level routines
     */
    pasynInterface = (asynInterface *)callocMustSucceed(2, sizeof *pasynInterface, "drvAsynSerialPortConfigure");
    tty->common.interfaceType = asynCommonType;
    tty->common.pinterface  = (void *)&drvAsynSerialPortAsynCommon;
    tty->common.drvPvt = tty;
    tty->option.interfaceType = asynOptionType;
    tty->option.pinterface  = (void *)&drvAsynSerialPortAsynOption;
    tty->option.drvPvt = tty;
    if (pasynManager->registerPort(tty->portName,
                                   ASYN_CANBLOCK,
                                   !noAutoConnect,
                                   priority,
                                   0) != asynSuccess) {
        printf("drvAsynSerialPortConfigure: Can't register myself.\n");
        ttyCleanup(tty);
        return -1;
    }
    status = pasynManager->registerInterface(tty->portName,&tty->common);
    if(status != asynSuccess) {
        printf("drvAsynSerialPortConfigure: Can't register common.\n");
        ttyCleanup(tty);
        return -1;
    }
    status = pasynManager->registerInterface(tty->portName,&tty->option);
    if(status != asynSuccess) {
        printf("drvAsynSerialPortConfigure: Can't register option.\n");
        ttyCleanup(tty);
        return -1;
    }
    pasynOctet->readRaw = readRaw;
    pasynOctet->writeRaw = writeRaw;
    pasynOctet->flush = flushIt;
    tty->octet.interfaceType = asynOctetType;
    tty->octet.pinterface  = pasynOctet;
    tty->octet.drvPvt = tty;
    status = pasynOctetBase->initialize(tty->portName,&tty->octet,
         (noProcessEos ? 0 : 1),(noProcessEos ? 0 : 1),1);
    if(status != asynSuccess) {
        printf("drvAsynSerialPortConfigure: Can't register octet.\n");
        ttyCleanup(tty);
        return -1;
    }
    tty->pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(tty->pasynUser,tty->portName,-1);
    if(status != asynSuccess) {
        printf("connectDevice failed %s\n",tty->pasynUser->errorMessage);
        ttyCleanup(tty);
        return -1;
    }
    return 0;
}

/*
 * IOC shell command registration
 */
static const iocshArg drvAsynSerialPortConfigureArg0 = { "port name",iocshArgString};
static const iocshArg drvAsynSerialPortConfigureArg1 = { "tty name",iocshArgString};
static const iocshArg drvAsynSerialPortConfigureArg2 = { "priority",iocshArgInt};
static const iocshArg drvAsynSerialPortConfigureArg3 = { "disable auto-connect",iocshArgInt};
static const iocshArg drvAsynSerialPortConfigureArg4 = { "noProcessEos",iocshArgInt};
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
