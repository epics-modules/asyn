/**********************************************************************
* Asyn device support using TCP stream port                           *
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
 * $Id: drvAsynTCPPort.c,v 1.4 2004-04-09 20:38:36 norume Exp $
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
#include <osiUnistd.h>
#include <epicsExport.h>
#include <asynDriver.h>
#include <asynInterposeEos.h>
#include <drvAsynTCPPort.h>

#if !defined(vxWorks) && !defined(__rtems__)
# include <sys/poll.h>
#endif

#define INBUFFER_SIZE         600
#define CANCEL_CHECK_INTERVAL 5.0 /* Interval between checks for I/O cancel */
#define CONSECUTIVE_READ_TIMEOUT_LIMIT  5   /* Disconnect after this many */

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
    osiSockAddr        farAddr;
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
drvAsynTCPPortReport(void *drvPvt, FILE *fp, int details)
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
}

/*
 * Create a link
 */
static asynStatus
drvAsynTCPPortConnect(void *drvPvt, asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int i;

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
     * Create the socket
     */
    if ((tty->fd = epicsSocketCreate(PF_INET, SOCK_STREAM, 0)) < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                              "Can't create socket: %s", strerror(errno));
        return asynError;
    }

    /*
     * Connect to the remote terminal server
     */
    epicsTimerStartDelay(tty->timer, 10.0);
    i = connect(tty->fd, &tty->farAddr.sa, sizeof tty->farAddr.ia);
    epicsTimerCancel(tty->timer);
    if (i < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                              "Can't connect to %s: %s",
                                    tty->serialDeviceName, strerror(errno));
        close(tty->fd);
        tty->fd = -1;
        return asynError;
    }
    i = 1;
    if (setsockopt(tty->fd, IPPROTO_TCP, TCP_NODELAY, &i, sizeof i) < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                               "Can't set %s socket NODELAY option: %s\n",
                                       tty->serialDeviceName, strerror(errno));
        close(tty->fd);
        tty->fd = -1;
        return asynError;
    }
#ifdef POLLIN
    if (((i = fcntl(tty->fd, F_GETFL, 0)) < 0)
     || (fcntl(tty->fd, F_SETFL, i | O_NONBLOCK) < 0)) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                               "Can't set %s O_NONBLOCK option: %s\n",
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
drvAsynTCPPortDisconnect(void *drvPvt, asynUser *pasynUser)
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
drvAsynTCPPortGetPortOption(void *drvPvt, asynUser *pasynUser,
                              const char *key, char *val, int valSize)
{
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                "Unsupported key \"%s\"", key);
    return asynError;
}

static asynStatus
drvAsynTCPPortSetPortOption(void *drvPvt, asynUser *pasynUser,
                              const char *key, const char *val)
{
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                "Unsupported key \"%s\"", key);
    return asynError;
}

/*
 * Write to the TCP port
 */
static int
drvAsynTCPPortWrite(void *drvPvt, asynUser *pasynUser, const char *data, int numchars)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int thisWrite;
    int nleft = numchars;
    int timerStarted = 0;

    assert(tty);
    assert(tty->fd >= 0);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                                       "%s write.\n", tty->serialDeviceName);
    asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, data, numchars,
                               "%s write %d ", tty->serialDeviceName, numchars);
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
#if defined(vxWorks) || defined(__rtems__)
        if (setsockopt(tty->fd, IPPROTO_TCP, SO_SNDTIMEO,
                        &tty->writePollmsec, sizeof tty->writePollmsec) < 0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                   "Can't set %s socket send timeout: %s",
                                   tty->serialDeviceName, strerror(errno));
            return -1;
        }
#endif
    }
    tty->cancelFlag = 0;
    tty->timeoutFlag = 0;
    nleft = numchars;
    if (tty->writeTimeout > 0) {
        epicsTimerStartDelay(tty->timer, tty->writeTimeout);
        timerStarted = 1;
    }
    for (;;) {
#ifdef POLLOUT
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
            break;
        }
        if (tty->timeoutFlag || (tty->writePollmsec == 0)) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                    "%s timeout", tty->serialDeviceName);
            break;
        }
        if ((thisWrite < 0) && (errno != EWOULDBLOCK)
                            && (errno != EINTR)
                            && (errno != EAGAIN)) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "%s write error: %s",
                                        tty->serialDeviceName, strerror(errno));
            closeConnection(tty);
            break;
        }
    }
    if (timerStarted)
        epicsTimerCancel(tty->timer);
    return numchars - nleft;
}

/*
 * Read from the TCP port
 */
static int
drvAsynTCPPortRead(void *drvPvt, asynUser *pasynUser, char *data, int maxchars)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int thisRead;
    int nRead = 0;
    int timerStarted = 0;

    assert(tty);
    assert(tty->fd >= 0);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
               "%s read.\n", tty->serialDeviceName);
    if (maxchars <= 0)
        return 0;
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
#if defined(vxWorks) || defined(__rtems__)
        if (setsockopt(tty->fd, IPPROTO_TCP, SO_RCVTIMEO,
                        &tty->readPollmsec, sizeof tty->readPollmsec) < 0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                   "Can't set %s socket receive timeout: %s",
                                   tty->serialDeviceName, strerror(errno));
            return -1;
        }
#endif
    }
    tty->cancelFlag = 0;
    tty->timeoutFlag = 0;
    for (;;) {
        if (!timerStarted && (tty->readTimeout > 0)) {
            epicsTimerStartDelay(tty->timer, tty->readTimeout);
            timerStarted = 1;
        }
#ifdef POLLIN
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
                nRead = -1;
                break;
            }
            if (tty->readTimeout == 0)
                tty->timeoutFlag = 1;
        }
        if (tty->cancelFlag) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                    "%s I/O cancelled", tty->serialDeviceName);
            break;
        }
        if (tty->timeoutFlag) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                    "%s timeout", tty->serialDeviceName);
            if (++tty->consecutiveReadTimeouts >= CONSECUTIVE_READ_TIMEOUT_LIMIT)
                closeConnection(tty);
            break;
        }
    }
    if (timerStarted)
        epicsTimerCancel(tty->timer);
    return nRead ? nRead : -1;
}

/*
 * Flush pending input
 */
static asynStatus
drvAsynTCPPortFlush(void *drvPvt,asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int flags;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
            "%s flush\n", tty->serialDeviceName);
    if (tty->fd >= 0) {
        /*
         * Toss characters until there are none left
         */
#ifdef vxWorks
        flags = 1;
        if (ioctl(tty->fd, FIONBIO, &flags) >= 0)
#else
        if (((flags = fcntl(tty->fd, F_GETFL, 0)) >= 0)
         && (fcntl(tty->fd, F_SETFL, flags | O_NONBLOCK) >= 0))
#endif
            {
            char cbuf[512];
            while (read(tty->fd, cbuf, sizeof cbuf) > 0)
                continue;
#ifdef vxWorks
            flags = 0;
            ioctl(tty->fd, FIONBIO, &flags);
#else
            fcntl(tty->fd, F_SETFL, flags);
#endif
            }
    }
    return asynSuccess;
}

/*
 * Set the end-of-string message
 */
static asynStatus
drvAsynTCPPortSetEos(void *drvPvt,asynUser *pasynUser,const char *eos,int eoslen)
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
drvAsynTCPPortGetEos(void *drvPvt,asynUser *pasynUser,char *eos,
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
static const struct asynCommon drvAsynTCPPortAsynCommon = {
    drvAsynTCPPortReport,
    drvAsynTCPPortConnect,
    drvAsynTCPPortDisconnect,
    drvAsynTCPPortSetPortOption,
    drvAsynTCPPortGetPortOption
};

/*
 * asynOctet methods
 */
static const struct asynOctet drvAsynTCPPortAsynOctet = {
    drvAsynTCPPortRead,
    drvAsynTCPPortWrite,
    drvAsynTCPPortFlush,
    drvAsynTCPPortSetEos,
    drvAsynTCPPortGetEos
};

/*
 * Configure and register a generic serial device
 */
int
drvAsynTCPPortConfigure(char *portName,
                     char *ttyName,
                     unsigned int priority,
                     int noAutoConnect)
{
    ttyController_t *tty;
    asynInterface *pasynInterface;
    asynStatus status;
    char *cp;
    int port;

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
    tty = (ttyController_t *)callocMustSucceed(1, sizeof *tty, "drvAsynTCPPortConfigure()");
    /*
     * Create timeout mechanism
     */
     tty->timer = epicsTimerQueueCreateTimer(
         pserialBase->timerQueue, timeoutHandler, tty);
     if(!tty->timer) {
        errlogPrintf("drvAsynTCPPortConfigure: Can't create timer.\n");
        return -1;
    }
    tty->fd = -1;
    tty->serialDeviceName = epicsStrDup(ttyName);
    tty->portName = epicsStrDup(portName);

    /*
     * Parse configuration parameters
     */
    memset(&tty->farAddr, 0, sizeof tty->farAddr);
    if ((cp = strchr(tty->serialDeviceName, ':')) == NULL) {
        errlogPrintf("drvAsynTCPPortConfigure: \"%s\" is not of the form \"<host>:<port>\"\n",
                                                        tty->serialDeviceName);
        return -1;
    }
        *cp = '\0';
    if(hostToIPAddr(tty->serialDeviceName, &tty->farAddr.ia.sin_addr) < 0) {
        *cp = ':';
        errlogPrintf("drvGenericSerialConfigure: Unknown host \"%s\".\n", tty->serialDeviceName);
        ttyCleanup(tty);
        return -1;
    }
    port = strtol(cp+1, NULL, 0);
    tty->farAddr.ia.sin_port = htons(port);
    tty->farAddr.ia.sin_family = AF_INET;
    *cp = ':';

    /*
     *  Link with higher level routines
     */
    pasynInterface = (asynInterface *)callocMustSucceed(2, sizeof *pasynInterface, "drvAsynTCPPortConfigure");
    tty->common.interfaceType = asynCommonType;
    tty->common.pinterface  = (void *)&drvAsynTCPPortAsynCommon;
    tty->common.drvPvt = tty;
    tty->octet.interfaceType = asynOctetType;
    tty->octet.pinterface  = (void *)&drvAsynTCPPortAsynOctet;
    tty->octet.drvPvt = tty;
    if (pasynManager->registerPort(tty->portName,
                                   0, /*not multiDevice*/
                                   !noAutoConnect,
                                   priority,
                                   0) != asynSuccess) {
        errlogPrintf("drvAsynTCPPortConfigure: Can't register myself.\n");
        ttyCleanup(tty);
        return -1;
    }
    if(pasynManager->registerInterface(tty->portName,&tty->common)!= asynSuccess) {
        errlogPrintf("drvAsynTCPPortConfigure: Can't register common.\n");
        ttyCleanup(tty);
        return -1;
    }
    if(pasynManager->registerInterface(tty->portName,&tty->octet)!= asynSuccess) {
        errlogPrintf("drvAsynTCPPortConfigure: Can't register octet.\n");
        ttyCleanup(tty);
        return -1;
    }
    tty->pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(tty->pasynUser,tty->portName,-1);
    if(status!=asynSuccess) {
        printf("connectDevice failed %s\n",tty->pasynUser->errorMessage);
        ttyCleanup(tty);
        return -1;
    }
    if (asynInterposeEosConfig(tty->portName, -1) < 0) {
        ttyCleanup(tty);
        return -1;
    }
    return 0;
}

/*
 * IOC shell command registration
 */
#include <iocsh.h>
static const iocshArg drvAsynTCPPortConfigureArg0 = { "port name",iocshArgString};
static const iocshArg drvAsynTCPPortConfigureArg1 = { "tty name",iocshArgString};
static const iocshArg drvAsynTCPPortConfigureArg2 = { "priority",iocshArgInt};
static const iocshArg drvAsynTCPPortConfigureArg3 = { "disable auto-connect",iocshArgInt};
static const iocshArg *drvAsynTCPPortConfigureArgs[] = {
    &drvAsynTCPPortConfigureArg0, &drvAsynTCPPortConfigureArg1,
    &drvAsynTCPPortConfigureArg2, &drvAsynTCPPortConfigureArg3};
static const iocshFuncDef drvAsynTCPPortConfigureFuncDef =
                      {"drvAsynTCPPortConfigure",4,drvAsynTCPPortConfigureArgs};
static void drvAsynTCPPortConfigureCallFunc(const iocshArgBuf *args)
{
    drvAsynTCPPortConfigure(args[0].sval, args[1].sval, args[2].ival,
                                                                args[3].ival);
}

/*
 * This routine is called before multitasking has started, so there's
 * no race condition in the test/set of firstTime.
 */
static void
drvAsynTCPPortRegisterCommands(void)
{
    static int firstTime = 1;
    if (firstTime) {
        iocshRegister(&drvAsynTCPPortConfigureFuncDef,drvAsynTCPPortConfigureCallFunc);
        firstTime = 0;
    }
}
epicsExportRegistrar(drvAsynTCPPortRegisterCommands);
