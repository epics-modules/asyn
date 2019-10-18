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
 * $Id: drvAsynSerialPort.c,v 1.49 2009-08-13 20:35:31 norume Exp $
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

#include "serial_rs485.h"

#ifdef vxWorks
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
    struct termios     termios;
#ifdef ASYN_RS485_SUPPORTED
    struct serial_rs485  rs485;
#endif
    int                baud;
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
 * Apply option settings to hardware
 */
static asynStatus
applyOptions(asynUser *pasynUser, ttyController_t *tty)
{
#ifdef vxWorks
    if ((ioctl(tty->fd, FIOBAUDRATE, tty->baud) < 0)
     && (ioctl(tty->fd, SIO_BAUD_SET, tty->baud) < 0)) {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                "SIO_BAUD_SET failed: %s", strerror(errno));
        return asynError;
    }
    if (ioctl(tty->fd, SIO_HW_OPTS_SET, tty->termios.c_cflag) < 0) {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                               "SIO_HW_OPTS_SET failed: %s", strerror(errno));
        return asynError;
    }
#else
    
    tty->termios.c_cflag |= CREAD;
    if (tcsetattr(tty->fd, TCSANOW, &tty->termios) < 0) {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                   "tcsetattr failed: %s", strerror(errno));
        return asynError;
    }
#endif

    return asynSuccess;
}

/*
 * asynOption methods
 */
static asynStatus
getOption(void *drvPvt, asynUser *pasynUser,
                              const char *key, char *val, int valSize)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int l;

    val[0] = '\0';
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
/* vxWorks uses CLOCAL when it should use CRTSCTS */
#ifdef vxWorks
            c = (tty->termios.c_cflag & CLOCAL) ? 'N' : 'Y';
#else
            c = (tty->termios.c_cflag & CRTSCTS) ? 'Y' : 'N';
#endif
        l = epicsSnprintf(val, valSize, "%c", c);
    }
    else if (epicsStrCaseCmp(key, "ixon") == 0) {
#ifdef vxWorks
        l = epicsSnprintf(val, valSize, "%c",  (ioctl(tty->fd, FIOGETOPTIONS, 0) & OPT_TANDEM) ? 'Y' : 'N');
#else
        l = epicsSnprintf(val, valSize, "%c",  (tty->termios.c_iflag & IXON) ? 'Y' : 'N');
#endif
    }
    else if (epicsStrCaseCmp(key, "ixany") == 0) {
#ifdef vxWorks
        l = epicsSnprintf(val, valSize, "%c",  'N');
#else
        l = epicsSnprintf(val, valSize, "%c",  (tty->termios.c_iflag & IXANY) ? 'Y' : 'N');
#endif
    }
    else if (epicsStrCaseCmp(key, "ixoff") == 0) {
#ifdef vxWorks
        l = epicsSnprintf(val, valSize, "%c",  'N');
#else
        l = epicsSnprintf(val, valSize, "%c",  (tty->termios.c_iflag & IXOFF) ? 'Y' : 'N');
#endif
    }
#ifdef ASYN_RS485_SUPPORTED
    else if (epicsStrCaseCmp(key, "rs485_enable") == 0) {
        l = epicsSnprintf(val, valSize, "%c",  (tty->rs485.flags & SER_RS485_ENABLED) ? 'Y' : 'N');
    }
    else if (epicsStrCaseCmp(key, "rs485_rts_on_send") == 0) {
        l = epicsSnprintf(val, valSize, "%c",  (tty->rs485.flags & SER_RS485_RTS_ON_SEND) ? 'Y' : 'N');
    }
    else if (epicsStrCaseCmp(key, "rs485_rts_after_send") == 0) {
        l = epicsSnprintf(val, valSize, "%c",  (tty->rs485.flags & SER_RS485_RTS_AFTER_SEND) ? 'Y' : 'N');
    }
    else if (epicsStrCaseCmp(key, "rs485_delay_rts_before_send") == 0) {
        l = epicsSnprintf(val, valSize, "%u", tty->rs485.delay_rts_before_send);
    }
    else if (epicsStrCaseCmp(key, "rs485_delay_rts_after_send") == 0) {
        l = epicsSnprintf(val, valSize, "%u", tty->rs485.delay_rts_after_send);
    }
#endif
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
setOption(void *drvPvt, asynUser *pasynUser, const char *key, const char *val)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    struct termios termiosPrev;
    int baudPrev;
#ifdef ASYN_RS485_SUPPORTED
    struct serial_rs485 rs485Prev;
    int rs485_changed = 0;
#endif

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                    "%s setOption key %s val %s\n", tty->portName, key, val);
    
    /* Make a copy of tty->termios and tty->baud so we can restore them in case of errors */                
    termiosPrev = tty->termios;
    baudPrev = tty->baud;
#ifdef ASYN_RS485_SUPPORTED
    rs485Prev = tty->rs485;
#endif

    if (epicsStrCaseCmp(key, "baud") == 0) {
        int baud;
        if(sscanf(val, "%d", &baud) != 1) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                                "Bad number");
            return asynError;
        }
#ifndef vxWorks         
        {
        speed_t baudCode;
/* On this system is the baud code the actual baud rate?  
 * If so use it directly, else compare against known baud codes */
#if (defined(B300) && (B300 == 300) && defined(B9600) && (B9600 == 9600))
        baudCode = baud;
#else
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
#ifdef B57600
            case 57600:  baudCode = B57600 ;  break;
#endif
#ifdef B115200
            case 115200: baudCode = B115200 ; break;
#endif
#ifdef B230400
            case 230400: baudCode = B230400 ; break;
#endif
#ifdef B460800
            case 460800: baudCode = B460800 ; break;
#endif
#ifdef B500000
            case 500000: baudCode = B500000 ; break;
#endif
#ifdef B576000
            case 576000: baudCode = B576000 ; break;
#endif
#ifdef B921600
            case 921600: baudCode = B921600 ; break;
#endif
#ifdef B1000000
            case 1000000: baudCode = B1000000 ; break;
#endif
#ifdef B1152000
            case 1152000: baudCode = B1152000 ; break;
#endif
#ifdef B1500000
            case 1500000: baudCode = B1500000 ; break;
#endif
#ifdef B2000000
            case 2000000: baudCode = B2000000 ; break;
#endif
#ifdef B2500000
            case 2500000: baudCode = B2500000 ; break;
#endif
#ifdef B3000000
            case 3000000: baudCode = B3000000 ; break;
#endif
#ifdef B3500000
            case 3500000: baudCode = B3500000 ; break;
#endif
#ifdef B4000000
            case 4000000: baudCode = B4000000 ; break;
#endif
            default:
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                      "Unsupported data rate (%d baud)", baud);
                return asynError;
        }
#endif /* Baudcode is baud */
        if(cfsetispeed(&tty->termios,baudCode) < 0 ) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "cfsetispeed returned %s",strerror(errno));
            return asynError;
        }
        if(cfsetospeed(&tty->termios,baudCode) < 0 ) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "cfsetospeed returned %s",strerror(errno));
            return asynError;
        }
        }
#endif /* vxWorks */
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
    else if (epicsStrCaseCmp(key, "ixon") == 0) {
        if (epicsStrCaseCmp(val, "Y") == 0) {
#ifdef vxWorks
            ioctl(tty->fd, FIOSETOPTIONS, ioctl(tty->fd, FIOGETOPTIONS, 0) | OPT_TANDEM);
            return asynSuccess;
#else
            tty->termios.c_iflag |= IXON;
#endif
        }
        else if (epicsStrCaseCmp(val, "N") == 0) {
#ifdef vxWorks
            ioctl(tty->fd, FIOSETOPTIONS, ioctl(tty->fd, FIOGETOPTIONS, 0) & ~OPT_TANDEM);
            return asynSuccess;
#else
            tty->termios.c_iflag &= ~IXON;
#endif
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                    "Invalid ixon value.");
            return asynError;
        }
    }
    else if (epicsStrCaseCmp(key, "ixany") == 0) {
#ifdef vxWorks
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                    "Option ixany not supported on vxWorks");
            return asynError;       
#else
        if (epicsStrCaseCmp(val, "Y") == 0) {
            tty->termios.c_iflag |= IXANY;
        }
        else if (epicsStrCaseCmp(val, "N") == 0) {
            tty->termios.c_iflag &= ~IXANY;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                    "Invalid ixany value.");
            return asynError;
        }
#endif
    }
    else if (epicsStrCaseCmp(key, "ixoff") == 0) {
#ifdef vxWorks
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                    "Option ixoff not supported on vxWorks");
            return asynError;       
#else
        if (epicsStrCaseCmp(val, "Y") == 0) {
            tty->termios.c_iflag |= IXOFF;
        }
        else if (epicsStrCaseCmp(val, "N") == 0) {
            tty->termios.c_iflag &= ~IXOFF;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                    "Invalid ixoff value.");
            return asynError;
        }
#endif
    }
#ifdef ASYN_RS485_SUPPORTED
    else if (epicsStrCaseCmp(key, "rs485_enable") == 0) {
        if (epicsStrCaseCmp(val, "Y") == 0) {
           tty->rs485.flags |= SER_RS485_ENABLED;
        }
        else if (epicsStrCaseCmp(val, "N") == 0) {
           tty->rs485.flags = 0;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                    "Invalid rs485_enable value.");
            return asynError;
        }
        rs485_changed = 1;
    }
    else if (epicsStrCaseCmp(key, "rs485_rts_on_send") == 0) {
        if (epicsStrCaseCmp(val, "Y") == 0) {
           tty->rs485.flags |= SER_RS485_RTS_ON_SEND;
        }
        else if (epicsStrCaseCmp(val, "N") == 0) {
           tty->rs485.flags &= ~(SER_RS485_RTS_ON_SEND);
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                    "Invalid rs485_rts_on_send value.");
            return asynError;
        }
        rs485_changed = 1;
    }
    else if (epicsStrCaseCmp(key, "rs485_rts_after_send") == 0) {
        if (epicsStrCaseCmp(val, "Y") == 0) {
           tty->rs485.flags |= SER_RS485_RTS_AFTER_SEND;
        }
        else if (epicsStrCaseCmp(val, "N") == 0) {
           tty->rs485.flags &= ~(SER_RS485_RTS_AFTER_SEND);
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                    "Invalid rs485_rts_on_send value.");
            return asynError;
        }
        rs485_changed = 1;
    }
    else if (epicsStrCaseCmp(key, "rs485_delay_rts_before_send") == 0) {
        unsigned delay;
        if(sscanf(val, "%u", &delay) != 1) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                                "Bad number");
            return asynError;
        }
        tty->rs485.delay_rts_before_send = delay;
        rs485_changed = 1;
    }
    else if (epicsStrCaseCmp(key, "rs485_delay_rts_after_send") == 0) {
        unsigned delay;
        if(sscanf(val, "%u", &delay) != 1) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                                "Bad number");
            return asynError;
        }
        tty->rs485.delay_rts_after_send = delay;
        rs485_changed = 1;
    }
#endif
    else if (epicsStrCaseCmp(key, "") != 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                "Unsupported key \"%s\"", key);
        return asynError;
    }
    if (tty->fd >= 0) {
        if (applyOptions(pasynUser, tty) != asynSuccess) {
            /* Restore previous values of tty->baud and tty->termios */
            tty->baud = baudPrev;
            tty->termios = termiosPrev;
            return asynError;
        }
#ifdef ASYN_RS485_SUPPORTED
        if (rs485_changed) {
            if( ioctl( tty->fd, TIOCSRS485, &tty->rs485 ) < 0 ) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                              "ioctl TIOCSRS485 failed: %s", strerror(errno));
                tty->rs485 = rs485Prev;
                return asynError;
            }
        }
#endif
    }

    return asynSuccess;
}
static const struct asynOption asynOptionMethods = { setOption, getOption };

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
    ioctl(tty->fd, FIOCANCEL, 0);
    /*
     * Since it is possible, though unlikely, that we got here before the
     * slow system call actually started, we arrange to poke the thread
     * again in a little while.
     */
    epicsTimerStartDelay(tty->timer, 10.0);
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
                            "%s Can't open  %s",
                                    tty->serialDeviceName, strerror(errno));
        return asynError;
    }
#if defined(FD_CLOEXEC) && !defined(vxWorks)
    if (fcntl(tty->fd, F_SETFD, FD_CLOEXEC) < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                            "Can't set %s close-on-exec flag: %s",
                                    tty->serialDeviceName, strerror(errno));
        close(tty->fd);
        tty->fd = -1;
        return asynError;
    }
#endif
    applyOptions(pasynUser, tty);
   
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


/*
 * Write to the serial line
 */
static asynStatus writeIt(void *drvPvt, asynUser *pasynUser,
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
                            "%s write %lu\n", tty->serialDeviceName, (unsigned long)numchars);
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
                closeConnection(pasynUser,tty);
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
    asynPrint(pasynUser, ASYN_TRACE_FLOW, "wrote %lu to %s, return %s\n",
                                            (unsigned long)*nbytesTransfered,
                                            tty->serialDeviceName,
                                            pasynManager->strStatus(status));
    return status;
}

/*
 * Read from the serial line
 */
static asynStatus readIt(void *drvPvt, asynUser *pasynUser,
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
            "%s maxchars %d Why <=0?",tty->serialDeviceName,(int)maxchars);
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
                closeConnection(pasynUser,tty);
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
            closeConnection(pasynUser,tty);
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
    asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s read %lu, return %d\n",
                            tty->serialDeviceName, (unsigned long)*nbytesTransfered, status);
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

static asynOctet asynOctetMethods = { writeIt, readIt, flushIt };

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
static const struct asynCommon asynCommonMethods = {
    report,
    connectIt,
    disconnect
};

/*
 * Configure and register a generic serial device
 */
epicsShareFunc int
drvAsynSerialPortConfigure(char *portName,
                     char *ttyName,
                     unsigned int priority,
                     int noAutoConnect,
                     int noProcessEos)
{
    ttyController_t *tty;
    asynStatus status;


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
    tty = (ttyController_t *)callocMustSucceed(1, sizeof(*tty), "drvAsynSerialPortConfigure()");

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
     * Set defaults
     */
    tty->termios.c_cflag = CS8 | CLOCAL | CREAD;
    tty->baud = 9600;
#ifndef vxWorks
    tty->termios.c_iflag = IGNBRK | IGNPAR;
    tty->termios.c_oflag = 0;
    tty->termios.c_lflag = 0;
    tty->termios.c_cc[VMIN] = 0;
    tty->termios.c_cc[VTIME] = 0;
    tty->termios.c_cc[VSTOP]  = 0x13; /* ^S */
    tty->termios.c_cc[VSTART] = 0x11; /* ^Q */

    cfsetispeed(&tty->termios, B9600);
    cfsetospeed(&tty->termios, B9600);
#endif

    /*
     *  Link with higher level routines
     */
    tty->common.interfaceType = asynCommonType;
    tty->common.pinterface  = (void *)&asynCommonMethods;
    tty->common.drvPvt = tty;
    tty->option.interfaceType = asynOptionType;
    tty->option.pinterface  = (void *)&asynOptionMethods;
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
    tty->octet.interfaceType = asynOctetType;
    tty->octet.pinterface  = &asynOctetMethods;
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
