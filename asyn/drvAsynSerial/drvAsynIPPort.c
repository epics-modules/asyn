/**********************************************************************
* Asyn device support using TCP stream or UDP datagram port           *
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
 * $Id: drvAsynIPPort.c,v 1.3 2004-07-12 05:49:58 rivers Exp $
 */

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <osiUnistd.h>
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
#include <asynOctet.h>
#include <asynInterposeEos.h>
#include <drvAsynIPPort.h>

#if defined(__rtems__)
# define USE_SOCKTIMEOUT
#else
# define USE_POLL
# if defined(vxWorks) || defined(_WIN32)
#  define FAKE_POLL
# else
#  include <sys/poll.h>
# endif
#endif

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
    int                socketType;
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

#ifdef FAKE_POLL
/*
 * Use select() to simulate enough of poll() to get by.
 */
#define POLLIN  0x1
#define POLLOUT 0x2
struct pollfd {
    int fd;
    short events;
    short revents;
};
static int poll(struct pollfd fds[], int nfds, int timeout)
{
    fd_set fdset;
    struct timeval tv;

    assert(nfds == 1);
    FD_ZERO(&fdset);
    FD_SET(fds[0].fd,&fdset);
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;
    return select(fds[0].fd + 1, 
        (fds[0].events & POLLIN) ? &fdset : NULL,
        (fds[0].events & POLLOUT) ? &fdset : NULL,
        NULL,
        &tv);
}
#endif

/*
 * OSI function to control blocking/non-blocking I/O
 */
static int setNonBlock(int fd, int nonBlockFlag)
{
#if defined(vxWorks)
    int flags;
    flags = nonBlockFlag;
    if (ioctl(fd, FIONBIO, &flags) < 0)
        return -1;
#elif defined(_WIN32)
    unsigned long int flags;
    flags = nonBlockFlag;
    if (socket_ioctl(fd, FIONBIO, &flags) < 0)
        return -1;
#else
    int flags;
    if ((flags = fcntl(fd, F_GETFL, 0)) < 0)
        return -1;
    if (nonBlockFlag)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) < 0)
        return -1;
#endif
    return 0;
}

/*
 * Report link parameters
 */
static void
drvAsynIPPortReport(void *drvPvt, FILE *fp, int details)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    fprintf(fp, "Port %s: %sonnected\n",
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
        epicsSocketDestroy(tty->fd);
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
drvAsynIPPortConnect(void *drvPvt, asynUser *pasynUser)
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
    if ((tty->fd = epicsSocketCreate(PF_INET, tty->socketType, 0)) < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                              "Can't create socket: %s", strerror(SOCKERRNO));
        return asynError;
    }

    /*
     * Connect to the remote host
     */
    epicsTimerStartDelay(tty->timer, 10.0);
    i = connect(tty->fd, &tty->farAddr.sa, sizeof tty->farAddr.ia);
    epicsTimerCancel(tty->timer);
    if (i < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                              "Can't connect to %s: %s",
                                    tty->serialDeviceName, strerror(SOCKERRNO));
        epicsSocketDestroy(tty->fd);
        tty->fd = -1;
        return asynError;
    }
    i = 1;
    if ((tty->socketType == SOCK_STREAM)
     && (setsockopt(tty->fd, IPPROTO_TCP, TCP_NODELAY, (void *)&i, sizeof i) < 0)) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                               "Can't set %s socket NODELAY option: %s\n",
                                       tty->serialDeviceName, strerror(SOCKERRNO));
        epicsSocketDestroy(tty->fd);
        tty->fd = -1;
        return asynError;
    }
#ifdef USE_POLL
    if (setNonBlock(tty->fd, 1) < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                               "Can't set %s O_NONBLOCK option: %s\n",
                                       tty->serialDeviceName, strerror(SOCKERRNO));
        epicsSocketDestroy(tty->fd);
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
drvAsynIPPortDisconnect(void *drvPvt, asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                                    "%s disconnect\n", tty->serialDeviceName);
    epicsTimerCancel(tty->timer);
    closeConnection(tty);
    return asynSuccess;
}

/*
 * Write to the TCP port
 */
static asynStatus drvAsynIPPortWrite(void *drvPvt, asynUser *pasynUser,
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
#ifdef USE_SOCKTIMEOUT
        if (setsockopt(tty->fd, IPPROTO_TCP, SO_SNDTIMEO,
                        &tty->writePollmsec, sizeof tty->writePollmsec) < 0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                   "Can't set %s socket send timeout: %s",
                                   tty->serialDeviceName, strerror(SOCKERRNO));
            return asynError;
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
#ifdef USE_POLL
        {
        struct pollfd pollfd;
        pollfd.fd = tty->fd;
        pollfd.events = POLLOUT;
        poll(&pollfd, 1, tty->writePollmsec);
        }
#endif
        thisWrite = send(tty->fd, (char *)data, nleft, 0);
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
        if ((thisWrite < 0) && (SOCKERRNO != SOCK_EWOULDBLOCK)
                            && (SOCKERRNO != SOCK_EINTR)) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "%s write error: %s",
                                        tty->serialDeviceName, strerror(SOCKERRNO));
            closeConnection(tty);
            status = asynError;
            break;
        }
    }
    if (timerStarted)
        epicsTimerCancel(tty->timer);
    *nbytesTransfered = numchars - nleft;
    return status;
}

/*
 * Read from the TCP port
 */
static asynStatus drvAsynIPPortRead(void *drvPvt, asynUser *pasynUser,
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
            "%s maxchars %d. Why <=0?\n",tty->serialDeviceName,maxchars);
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
#ifdef USE_SOCKTIMEOUT
        if (setsockopt(tty->fd, IPPROTO_TCP, SO_RCVTIMEO,
                        &tty->readPollmsec, sizeof tty->readPollmsec) < 0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                   "Can't set %s socket receive timeout: %s",
                                   tty->serialDeviceName, strerror(SOCKERRNO));
            status = asynError;
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
#ifdef USE_POLL
        {
        struct pollfd pollfd;
        pollfd.fd = tty->fd;
        pollfd.events = POLLIN;
        poll(&pollfd, 1, tty->readPollmsec);
        }
#endif
        thisRead = recv(tty->fd, data, maxchars, 0);
        if (thisRead > 0) {
            asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, data, thisRead,
                       "%s read %d ", tty->serialDeviceName, thisRead);
            tty->consecutiveReadTimeouts = 0;
            nRead = thisRead;
            tty->nRead += thisRead;
            break;
        }
        else {
            if ((thisRead < 0) && (SOCKERRNO != SOCK_EWOULDBLOCK)
                               && (SOCKERRNO != SOCK_EINTR)) {
                epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "%s read error: %s",
                                        tty->serialDeviceName, strerror(SOCKERRNO));
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
drvAsynIPPortFlush(void *drvPvt,asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    char cbuf[512];

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s flush\n", tty->serialDeviceName);
    if (tty->fd >= 0) {
        /*
         * Toss characters until there are none left
         */
#ifndef USE_POLL
        setNonBlock(tty->fd, 1);
#endif
        while (recv(tty->fd, cbuf, sizeof cbuf, 0) > 0)
            continue;
#ifndef USE_POLL
        setNonBlock(tty->fd, 0);
#endif
    }
    return asynSuccess;
}

/*
 * Set the end-of-string message
 */
static asynStatus
drvAsynIPPortSetEos(void *drvPvt,asynUser *pasynUser,const char *eos,int eoslen)
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
drvAsynIPPortGetEos(void *drvPvt,asynUser *pasynUser,char *eos,
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
            epicsSocketDestroy(tty->fd);
        free(tty->portName);
        free(tty->serialDeviceName);
        free(tty);
    }
}

/*
 * asynCommon methods
 */
static const struct asynCommon drvAsynIPPortAsynCommon = {
    drvAsynIPPortReport,
    drvAsynIPPortConnect,
    drvAsynIPPortDisconnect
};

/*
 * asynOctet methods
 */
static const struct asynOctet drvAsynIPPortAsynOctet = {
    drvAsynIPPortRead,
    drvAsynIPPortWrite,
    drvAsynIPPortFlush,
    drvAsynIPPortSetEos,
    drvAsynIPPortGetEos
};

/*
 * Configure and register a generic serial device
 */
int
drvAsynIPPortConfigure(const char *portName,
                     const char *hostInfo,
                     unsigned int priority,
                     int noAutoConnect,
                     int noEosProcessing)
{
    ttyController_t *tty;
    asynInterface *pasynInterface;
    asynStatus status;
    char *cp;
    int port;
    char protocol[6];

    /*
     * Check arguments
     */
    if (portName == NULL) {
        errlogPrintf("Port name missing.\n");
        return -1;
    }
    if (hostInfo == NULL) {
        errlogPrintf("TCP host information missing.\n");
        return -1;
    }

    if(!pserialBase) serialBaseInit();
    /*
     * Create a driver
     */
    tty = (ttyController_t *)callocMustSucceed(1, sizeof *tty, "drvAsynIPPortConfigure()");

    /*
     * Create timeout mechanism
     */
     tty->timer = epicsTimerQueueCreateTimer(
         pserialBase->timerQueue, timeoutHandler, tty);
     if(!tty->timer) {
        errlogPrintf("drvAsynIPPortConfigure: Can't create timer.\n");
        return -1;
    }
    tty->fd = -1;
    tty->serialDeviceName = epicsStrDup(hostInfo);
    tty->portName = epicsStrDup(portName);

    /*
     * Parse configuration parameters
     */
    protocol[0] = '\0';
    if (((cp = strchr(tty->serialDeviceName, ':')) == NULL)
     || (sscanf(cp, ":%d %5s", &port, protocol) < 1)) {
        errlogPrintf("drvAsynIPPortConfigure: \"%s\" is not of the form \"<host>:<port> [protocol]\"\n",
                                                        tty->serialDeviceName);
        return -1;
    }
    *cp = '\0';
    memset(&tty->farAddr, 0, sizeof tty->farAddr);
    if(hostToIPAddr(tty->serialDeviceName, &tty->farAddr.ia.sin_addr) < 0) {
        *cp = ':';
        errlogPrintf("drvAsynIPPortConfigure: Unknown host \"%s\".\n", tty->serialDeviceName);
        ttyCleanup(tty);
        return -1;
    }
    *cp = ':';
    tty->farAddr.ia.sin_port = htons(port);
    tty->farAddr.ia.sin_family = AF_INET;
    if ((protocol[0] ==  '\0')
     || (epicsStrCaseCmp(protocol, "tcp") == 0)) {
        tty->socketType = SOCK_STREAM;
    }
    else if (epicsStrCaseCmp(protocol, "udp") == 0) {
        tty->socketType = SOCK_DGRAM;
    }
    else {
        errlogPrintf("drvAsynIPPortConfigure: Unknown protocol \"%s\".\n", protocol);
        ttyCleanup(tty);
        return -1;
    }

    /*
     *  Link with higher level routines
     */
    pasynInterface = (asynInterface *)callocMustSucceed(2, sizeof *pasynInterface, "drvAsynIPPortConfigure");
    tty->common.interfaceType = asynCommonType;
    tty->common.pinterface  = (void *)&drvAsynIPPortAsynCommon;
    tty->common.drvPvt = tty;
    tty->octet.interfaceType = asynOctetType;
    tty->octet.pinterface  = (void *)&drvAsynIPPortAsynOctet;
    tty->octet.drvPvt = tty;
    if (pasynManager->registerPort(tty->portName,
                                   0, /*not multiDevice*/
                                   !noAutoConnect,
                                   priority,
                                   0) != asynSuccess) {
        errlogPrintf("drvAsynIPPortConfigure: Can't register myself.\n");
        ttyCleanup(tty);
        return -1;
    }
    if(pasynManager->registerInterface(tty->portName,&tty->common)!= asynSuccess) {
        errlogPrintf("drvAsynIPPortConfigure: Can't register common.\n");
        ttyCleanup(tty);
        return -1;
    }
    if(pasynManager->registerInterface(tty->portName,&tty->octet)!= asynSuccess) {
        errlogPrintf("drvAsynIPPortConfigure: Can't register octet.\n");
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
static const iocshArg drvAsynIPPortConfigureArg0 = { "port name",iocshArgString};
static const iocshArg drvAsynIPPortConfigureArg1 = { "host:port [protocol]",iocshArgString};
static const iocshArg drvAsynIPPortConfigureArg2 = { "priority",iocshArgInt};
static const iocshArg drvAsynIPPortConfigureArg3 = { "disable auto-connect",iocshArgInt};
static const iocshArg drvAsynIPPortConfigureArg4 = { "disable EOS processing",iocshArgInt};
static const iocshArg *drvAsynIPPortConfigureArgs[] = {
    &drvAsynIPPortConfigureArg0, &drvAsynIPPortConfigureArg1,
    &drvAsynIPPortConfigureArg2, &drvAsynIPPortConfigureArg3,
    &drvAsynIPPortConfigureArg4};
static const iocshFuncDef drvAsynIPPortConfigureFuncDef =
                      {"drvAsynIPPortConfigure",5,drvAsynIPPortConfigureArgs};
static void drvAsynIPPortConfigureCallFunc(const iocshArgBuf *args)
{
    drvAsynIPPortConfigure(args[0].sval, args[1].sval, args[2].ival,
                                         args[3].ival, args[4].ival);
}

/*
 * This routine is called before multitasking has started, so there's
 * no race condition in the test/set of firstTime.
 */
static void
drvAsynIPPortRegisterCommands(void)
{
    static int firstTime = 1;
    if (firstTime) {
        iocshRegister(&drvAsynIPPortConfigureFuncDef,drvAsynIPPortConfigureCallFunc);
        firstTime = 0;
    }
}
epicsExportRegistrar(drvAsynIPPortRegisterCommands);
