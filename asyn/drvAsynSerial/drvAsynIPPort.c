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
 * drvAsynIPPort.c,v 1.56 2009/08/13 19:11:44 rivers Exp
 */

/* Previous versions of drvAsynIPPort.c (1.29 and earlier, asyn R4-5 and earlier)
 * attempted to allow 2 things:
 * 1) Use an EPICS timer to time-out an I/O operation.
 * 2) Periodically check (every 5 seconds) during a long I/O operation to see if
 *    the operation should be cancelled.
 *
 * Item 1) above was not really implemented because there is no portable robust way
 * to abort an I/O operation.  So the timer set a flag which was checked after
 * the poll() was complete to see if the timeout had occured.  This was not robust,
 * because there were competing timers (timeout timer and poll) which could fire in
 * the wrong order.
 *
 * Item 2) was not implemented, because asyn has no mechanism to issue a cancel
 * request to a driver which is blocked on an I/O operation.
 *
 * Since neither of these mechanisms was working as designed, the driver has been 
 * re-written to simplify it.  If one or both of these are to be implemented in the future
 * the code as of version 1.29 should be used as the starting point.
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
#include <epicsExit.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <epicsThread.h>
#include <epicsTime.h>
#include <osiUnistd.h>

#include <epicsExport.h>
#include "asynDriver.h"
#include "asynOctet.h"
#include "asynOption.h"
#include "asynInterposeCom.h"
#include "asynInterposeEos.h"
#include "drvAsynIPPort.h"

#if !defined(_WIN32) && !defined(vxWorks) && defined(AF_UNIX)
# define HAS_AF_UNIX 1
#endif

#if defined(HAS_AF_UNIX)
# include <sys/un.h>
#endif

#if defined(__rtems__)
# define USE_SOCKTIMEOUT
#else
# define USE_POLL
# if defined(vxWorks)
#  define FAKE_POLL
# elif defined(_WIN32)
#  if defined(POLLIN)
#   define poll(fd,nfd,t) WSAPoll(fd,nfd,t)
#  else
#   define FAKE_POLL
#  endif
# else
#  include <sys/poll.h>
# endif
#endif


/* This delay is needed in cleanup() else sockets are not always really closed cleanly */
#define CLOSE_SOCKET_DELAY 0.02
/* This delay is how long to wait in seconds after a send fails with errno ==
 * EAGAIN or EINTR before trying again */
#define SEND_RETRY_DELAY 0.01

#define ISCOM_UNKNOWN (-1)

/*
 * This structure holds the hardware-specific information for a single
 * asyn link.  There is one for each IP socket.
 */
typedef struct {
    asynUser          *pasynUser;
    char              *IPDeviceName;
    char              *IPHostName;
    char              *portName;
    int                socketType;
    int                flags;
    int                isCom;
    int                disconnectOnReadTimeout;
    SOCKET             fd;
    unsigned long      nRead;
    unsigned long      nWritten;
    union {
      osiSockAddr        oa;
#if defined(HAS_AF_UNIX)
      struct sockaddr_un ua;
#endif
    }                  farAddr;
    size_t             farAddrSize;
    osiSockAddr        localAddr;
    size_t             localAddrSize;
    asynInterface      common;
    asynInterface      option;
    asynInterface      octet;
} ttyController_t;

#define FLAG_BROADCAST                  0x1
#define FLAG_CONNECT_PER_TRANSACTION    0x2
#define FLAG_SHUTDOWN                   0x4
#define FLAG_NEED_LOOKUP                0x100
#define FLAG_DONE_LOOKUP                0x200

#ifdef FAKE_POLL
/*
 * Use select() to simulate enough of poll() to get by.
 */
#define POLLIN  0x1
#define POLLOUT 0x2
struct pollfd {
    SOCKET fd;
    short events;
    short revents;
};
static int poll(struct pollfd fds[], int nfds, int timeout)
{
    fd_set fdset;
    struct timeval tv, *ptv;

    assert(nfds == 1);
    FD_ZERO(&fdset);
    FD_SET(fds[0].fd,&fdset);
    if (timeout >= 0) {
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;
        ptv = &tv;
    } else {
        ptv = NULL;
    }
    return select(fds[0].fd + 1, 
        (fds[0].events & POLLIN) ? &fdset : NULL,
        (fds[0].events & POLLOUT) ? &fdset : NULL,
        NULL,
        ptv);
}
#endif

/*
 * OSI function to control blocking/non-blocking I/O
 */
static int setNonBlock(SOCKET fd, int nonBlockFlag)
{
#if defined(vxWorks)
    int flags;
    flags = nonBlockFlag;
    if (ioctl(fd, FIONBIO, (int)&flags) < 0)
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
 * Close a connection
 */
static void
closeConnection(asynUser *pasynUser,ttyController_t *tty,const char *why)
{
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "Close %s connection (fd %d): %s\n", tty->IPDeviceName, tty->fd, why);
    if (tty->fd != INVALID_SOCKET) {
        epicsSocketDestroy(tty->fd);
        tty->fd = INVALID_SOCKET;
    }
    if (!(tty->flags & FLAG_CONNECT_PER_TRANSACTION) ||
         (tty->flags & FLAG_SHUTDOWN))
        pasynManager->exceptionDisconnect(pasynUser);
}

/*Beginning of asynCommon methods*/
/*
 * Report link parameters
 */
static void
asynCommonReport(void *drvPvt, FILE *fp, int details)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    if (details >= 1) {
        fprintf(fp, "    Port %s: %sonnected\n",
                                                tty->IPDeviceName,
                                                tty->fd != INVALID_SOCKET ? "C" : "Disc");
    }
    if (details >= 2) {
        fprintf(fp, "                    fd: %d\n", tty->fd);
        fprintf(fp, "    Characters written: %lu\n", tty->nWritten);
        fprintf(fp, "       Characters read: %lu\n", tty->nRead);
    }
}

/*
 * Clean up a socket on exit
 * This helps reduce problems with vxWorks when the IOC restarts
 */
static void
cleanup (void *arg)
{
    asynStatus status;
    ttyController_t *tty = (ttyController_t *)arg;

    if (!tty) return;
    status=pasynManager->lockPort(tty->pasynUser);
    if(status!=asynSuccess)
        asynPrint(tty->pasynUser, ASYN_TRACE_ERROR, "%s: cleanup locking error\n", tty->portName);

    if (tty->fd != INVALID_SOCKET) {
        asynPrint(tty->pasynUser, ASYN_TRACE_FLOW, "%s: shutdown socket\n", tty->portName);
        tty->flags |= FLAG_SHUTDOWN; /* prevent reconnect */
        epicsSocketDestroy(tty->fd);
        tty->fd = INVALID_SOCKET;
        /* If this delay is not present then the sockets are not always really closed cleanly */
        epicsThreadSleep(CLOSE_SOCKET_DELAY);
    }

    if(status==asynSuccess)
        pasynManager->unlockPort(tty->pasynUser);
}

/*
 * Parse a hostInfo string
 * This function can be called multiple times.  If there is an existing socket it closes it first.
*/
static int parseHostInfo(ttyController_t *tty, const char* hostInfo)
{
    char *cp;
    int isCom = 0;
    static const char *functionName = "drvAsynIPPort::parseHostInfo";

    if (tty->fd != INVALID_SOCKET) {
        tty->flags |= FLAG_SHUTDOWN; /* prevent reconnect and force calling exceptionDisconnect */
        closeConnection(tty->pasynUser, tty, "drvAsynIPPort::parseHostInfo, closing socket to open new connection");
        /* If this delay is not present then the sockets are not always really closed cleanly */
        epicsThreadSleep(CLOSE_SOCKET_DELAY);
    }
    tty->fd = INVALID_SOCKET;
    tty->flags = FLAG_SHUTDOWN;  /* This prevents connectIt from connecting if hostInfo parsing fails */
    tty->nRead = 0;
    tty->nWritten = 0;
    if (tty->IPDeviceName) {
        free(tty->IPDeviceName);
        tty->IPDeviceName = NULL;
    }
    if (tty->IPHostName) {
        free(tty->IPHostName);
        tty->IPHostName = NULL;
    }
    tty->IPDeviceName = epicsStrDup(hostInfo);

    /*
     * Parse configuration parameters
     */
    if (strncmp(tty->IPDeviceName, "unix://", 7) == 0) {
#   if defined(HAS_AF_UNIX)
        const char *cp = tty->IPDeviceName + 7;
        size_t l = strlen(cp);
        if ((l == 0) || (l >= sizeof(tty->farAddr.ua.sun_path)-1)) {
            printf("Path name \"%s\" invalid.\n", cp);
            return -1;
        }
        tty->farAddr.ua.sun_family = AF_UNIX;
        strcpy(tty->farAddr.ua.sun_path, cp);
        tty->farAddrSize = sizeof(tty->farAddr.ua) -
                           sizeof(tty->farAddr.ua.sun_path) + l + 1;
        tty->socketType = SOCK_STREAM;
#   else
        printf("%s: AF_UNIX not available on this platform.\n", functionName);
        return -1;
#   endif
    }
    else {
        int port;
        int localPort = -1;
        char protocol[6];
        char *secondColon, *blank;
        protocol[0] = '\0';
        if ((cp = strchr(tty->IPDeviceName, ':')) == NULL) {
            printf("%s: \"%s\" is not of the form \"<host>:<port>[:localPort] [protocol]\"\n",
                functionName, tty->IPDeviceName);
            return -1;
        }
        *cp = '\0';
        tty->IPHostName = epicsStrDup(tty->IPDeviceName);
        *cp = ':';
        if (sscanf(cp, ":%d", &port) < 1) {
            printf("%s: \"%s\" is not of the form \"<host>:<port>[:localPort] [protocol]\"\n",
                functionName, tty->IPDeviceName);
            return -1;
        }
        if ((secondColon = strchr(cp+1, ':')) != NULL) {
            if (sscanf(secondColon, ":%d", &localPort) < 1) {
                printf("%s: \"%s\" is not of the form \"<host>:<port>[:localPort] [protocol]\"\n",
                    functionName, tty->IPDeviceName);
                return -1;
            }
            tty->localAddr.ia.sin_family = AF_INET;
            tty->localAddr.ia.sin_port = htons(localPort);
            tty->localAddrSize = sizeof(tty->localAddr.ia);
        }
        if ((blank = strchr(cp, ' ')) != NULL) {
            sscanf(blank+1, "%5s", protocol);
        }
        tty->farAddr.oa.ia.sin_family = AF_INET;
        tty->farAddr.oa.ia.sin_port = htons(port);
        tty->farAddrSize = sizeof(tty->farAddr.oa.ia);
        tty->flags |= FLAG_NEED_LOOKUP;
        if ((protocol[0] ==  '\0')
         || (epicsStrCaseCmp(protocol, "tcp") == 0)) {
            tty->socketType = SOCK_STREAM;
        }
        else if (epicsStrCaseCmp(protocol, "com") == 0) {
            isCom = 1;
            tty->socketType = SOCK_STREAM;
        }
        else if (epicsStrCaseCmp(protocol, "http") == 0) {
            tty->socketType = SOCK_STREAM;
            tty->flags |= FLAG_CONNECT_PER_TRANSACTION;
        }
        else if (epicsStrCaseCmp(protocol, "udp") == 0) {
            tty->socketType = SOCK_DGRAM;
        }
        else if (epicsStrCaseCmp(protocol, "udp*") == 0) {
            tty->socketType = SOCK_DGRAM;
            tty->flags |= FLAG_BROADCAST;
        }
        else {
            printf("%s: Unknown protocol \"%s\".\n", functionName, protocol);
            return -1;
        }
    }
    if (tty->isCom == ISCOM_UNKNOWN) {
        tty->isCom = isCom;
    } else if (isCom != tty->isCom) {
        printf("%s: Ignoring attempt to change COM flag to %d from %d\n", 
                                               functionName, isCom, tty->isCom);
    }
    /* Successfully parsed socket information, turn off FLAG_SHUTDOWN */
    tty->flags &= ~FLAG_SHUTDOWN;
    return 0;
}

/*
 * Create a link
*/
static asynStatus
connectIt(void *drvPvt, asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    SOCKET fd;
    int i;

    /*
     * Sanity check
     */
    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "Open connection to %s  reason:%d  fd:%d\n", tty->IPDeviceName,
                                                           pasynUser->reason,
                                                           tty->fd);

    if (tty->fd != INVALID_SOCKET) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                              "%s: Link already open!", tty->IPDeviceName);
        return asynError;
    } else if(tty->flags & FLAG_SHUTDOWN) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                              "%s: Link shutdown!", tty->IPDeviceName);
        return asynError;
    }

    /* If pasynUser->reason > 0) then use this as the file descriptor */
    if (pasynUser->reason > 0) {
        fd = pasynUser->reason;
    } else {

        /*
         * Create the socket
         */
        if ((fd = epicsSocketCreate(tty->farAddr.oa.sa.sa_family, tty->socketType, 0)) < 0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                  "Can't create socket: %s", strerror(SOCKERRNO));
            return asynError;
        }

        /*
         * Enable broadcasts if so requested
         */
        i = 1;
        if ((tty->flags & FLAG_BROADCAST)
         && (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (void *)&i, sizeof i) < 0)) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                          "Can't set %s socket BROADCAST option: %s",
                          tty->IPDeviceName, strerror(SOCKERRNO));
            epicsSocketDestroy(fd);
            return asynError;
        }

        /*
         * Convert host name/number to IP address.
         * We delay doing this until now in case a device
         * has just appeared in a DNS database.
         */
        if (tty->flags & FLAG_NEED_LOOKUP) {
            if(hostToIPAddr(tty->IPHostName, &tty->farAddr.oa.ia.sin_addr) < 0) {
                epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                            "Unknown host \"%s\"", tty->IPHostName);
                epicsSocketDestroy(fd);
                return asynError;
            }
            tty->flags &= ~FLAG_NEED_LOOKUP;
            tty->flags |=  FLAG_DONE_LOOKUP;
        }
        
        /*
         * Bind to the local IP address if it was specified.
         * This is a very unusual configuration
         */
        if (tty->localAddrSize > 0) {
            if (bind(fd, &tty->localAddr.sa, tty->localAddrSize)) {
                epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                            "unable to bind to local port: %s", strerror(SOCKERRNO));
                epicsSocketDestroy(fd);
                return asynError;
            }
        }

        /*
         * Connect to the remote host
         * If the connect fails, arrange for another DNS lookup in case the
         * problem is just that the device has DHCP'd itself an new number.
         */
        if (tty->socketType != SOCK_DGRAM) {
            if (connect(fd, &tty->farAddr.oa.sa, (int)tty->farAddrSize) < 0) {
                epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                              "Can't connect to %s: %s",
                              tty->IPDeviceName, strerror(SOCKERRNO));
                epicsSocketDestroy(fd);
                if (tty->flags & FLAG_DONE_LOOKUP)
                    tty->flags |=  FLAG_NEED_LOOKUP;
                return asynError;
            }
        }
    }
    i = 1;
    if ((tty->socketType == SOCK_STREAM)
     && (tty->farAddr.oa.sa.sa_family == AF_INET)
     && (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *)&i, sizeof i) < 0)) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                      "Can't set %s socket NODELAY option: %s",
                      tty->IPDeviceName, strerror(SOCKERRNO));
        epicsSocketDestroy(fd);
        return asynError;
    }
#ifdef USE_POLL
    if (setNonBlock(fd, 1) < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                               "Can't set %s O_NONBLOCK option: %s",
                                       tty->IPDeviceName, strerror(SOCKERRNO));
        epicsSocketDestroy(fd);
        return asynError;
    }
#endif

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                          "Opened connection to %s\n", tty->IPDeviceName);
    tty->fd = fd;
    return asynSuccess;
}

static asynStatus
asynCommonConnect(void *drvPvt, asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    asynStatus status = asynSuccess;

    if (!(tty->flags & FLAG_CONNECT_PER_TRANSACTION))
        status = connectIt(drvPvt, pasynUser);
    if (status == asynSuccess)
        pasynManager->exceptionConnect(pasynUser);
    return status;
}

static asynStatus
asynCommonDisconnect(void *drvPvt, asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    closeConnection(pasynUser,tty,"Disconnect request");
    if (tty->flags & FLAG_CONNECT_PER_TRANSACTION)
        pasynManager->exceptionDisconnect(pasynUser);
    return asynSuccess;
}

/*Beginning of asynOctet methods*/
/*
 * Write to the TCP port
 */
static asynStatus writeIt(void *drvPvt, asynUser *pasynUser,
    const char *data, size_t numchars,size_t *nbytesTransfered)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int thisWrite;
    asynStatus status = asynSuccess;
    int writePollmsec;
    int epicsTimeStatus;
    epicsTimeStamp startTime;
    epicsTimeStamp endTime;
    int haveStartTime;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "%s write.\n", tty->IPDeviceName);
    asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, data, numchars,
                "%s write %lu\n", tty->IPDeviceName, (unsigned long)numchars);
    *nbytesTransfered = 0;
    if (tty->fd == INVALID_SOCKET) {
        if (tty->flags & FLAG_CONNECT_PER_TRANSACTION) {
            if ((status = connectIt(drvPvt, pasynUser)) != asynSuccess)
                return status;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                          "%s disconnected:", tty->IPDeviceName);
            return asynError;
        }
    }
    if (numchars == 0)
        return asynSuccess;
    writePollmsec = (int) (pasynUser->timeout * 1000.0);
    if (writePollmsec == 0) writePollmsec = 1;
    if (writePollmsec < 0) writePollmsec = -1;
#ifdef USE_SOCKTIMEOUT
    {
    struct timeval tv;
    tv.tv_sec = writePollmsec / 1000;
    tv.tv_usec = (writePollmsec % 1000) * 1000;
    if (setsockopt(tty->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv) < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                      "Can't set %s socket send timeout: %s",
                      tty->IPDeviceName, strerror(SOCKERRNO));
        return asynError;
    }
    }
#endif
    haveStartTime = 0;
    for (;;) {
#ifdef USE_POLL
        struct pollfd pollfd;
        pollfd.fd = tty->fd;
        pollfd.events = POLLOUT;
        epicsTimeGetCurrent(&startTime);
        while (poll(&pollfd, 1, writePollmsec) < 0) {
            if (errno != EINTR) {
                epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                          "Poll() failed: %s", strerror(errno));
                return asynError;
            }
            epicsTimeGetCurrent(&endTime);
            if (epicsTimeDiffInSeconds(&endTime, &startTime)*1000 > writePollmsec) break; 
        }
#endif
        for (;;) {
            if (tty->socketType == SOCK_DGRAM) {
                thisWrite = sendto(tty->fd, (char *)data, (int)numchars, 0, &tty->farAddr.oa.sa, (int)tty->farAddrSize);
            } else {
                thisWrite = send(tty->fd, (char *)data, (int)numchars, 0);
            }
            if (thisWrite >= 0) break;
            if (SOCKERRNO == SOCK_EWOULDBLOCK || SOCKERRNO == SOCK_EINTR) {
                if (!haveStartTime) {
                    epicsTimeStatus = epicsTimeGetCurrent(&startTime);
                    assert(epicsTimeStatus == epicsTimeOK);
                    haveStartTime = 1;
                } else if (pasynUser->timeout >= 0) {
                    epicsTimeStatus = epicsTimeGetCurrent(&endTime);
                    assert(epicsTimeStatus == epicsTimeOK);
                    if (epicsTimeDiffInSeconds(&endTime, &startTime) >
                            pasynUser->timeout) {
                        thisWrite = 0;
                        break;
                    }
                }
                epicsThreadSleep(SEND_RETRY_DELAY);
            } else break;
        }
        if (thisWrite > 0) {
            tty->nWritten += (unsigned long)thisWrite;
            *nbytesTransfered += thisWrite;
            numchars -= thisWrite;
            if (numchars == 0)
                break;
            data += thisWrite;
        }
        else if (thisWrite == 0) {
            status = asynTimeout;
            break;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                     "%s write error: %s", tty->IPDeviceName,
                                                           strerror(SOCKERRNO));
            closeConnection(pasynUser,tty,"Write error");
            status = asynError;
            break;
        }
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "wrote %lu to %s, return %s.\n", (unsigned long)*nbytesTransfered,
                                               tty->IPDeviceName,
                                               pasynManager->strStatus(status));
    return status;
}

/*
 * Read from the TCP port
 */
static asynStatus readIt(void *drvPvt, asynUser *pasynUser,
    char *data, size_t maxchars,size_t *nbytesTransfered,int *gotEom)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int thisRead;
    int readPollmsec;
    int reason = 0;
    epicsTimeStamp startTime;
    epicsTimeStamp endTime;
    asynStatus status = asynSuccess;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "%s read.\n", tty->IPDeviceName);
    if (tty->fd == INVALID_SOCKET) {
        if (tty->flags & FLAG_CONNECT_PER_TRANSACTION) {
            if ((status = connectIt(drvPvt, pasynUser)) != asynSuccess)
                return status;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                          "%s disconnected:", tty->IPDeviceName);
            return asynError;
        }
    }
    if (maxchars <= 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                  "%s maxchars %d. Why <=0?",tty->IPDeviceName,(int)maxchars);
        return asynError;
    }
    readPollmsec = (int) (pasynUser->timeout * 1000.0);
    if (readPollmsec == 0) readPollmsec = 1;
    if (readPollmsec < 0) readPollmsec = -1;
#ifdef USE_SOCKTIMEOUT
    {
    struct timeval tv;
    tv.tv_sec = readPollmsec / 1000;
    tv.tv_usec = (readPollmsec % 1000) * 1000;
    if (setsockopt(tty->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                      "Can't set %s socket receive timeout: %s",
                      tty->IPDeviceName, strerror(SOCKERRNO));
        status = asynError;
    }
    }
#endif
    if (gotEom) *gotEom = 0;
#ifdef USE_POLL
    {
        struct pollfd pollfd;
        pollfd.fd = tty->fd;
        pollfd.events = POLLIN;
        epicsTimeGetCurrent(&startTime);
        while (poll(&pollfd, 1, readPollmsec) < 0) {
            if (errno != EINTR) {
                epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                          "Poll() failed: %s", strerror(errno));
                return asynError;
            }
            epicsTimeGetCurrent(&endTime);
            if (epicsTimeDiffInSeconds(&endTime, &startTime)*1000. > readPollmsec) break; 
        }
    }
#endif
    if (tty->socketType == SOCK_DGRAM) {
        /* We use recvfrom() for SOCK_DRAM so we can print the source address with ASYN_TRACEIO_DRIVER */
        osiSockAddr oa;
        unsigned int addrlen = sizeof(oa.ia);
        thisRead = recvfrom(tty->fd, data, (int)maxchars, 0, &oa.sa, &addrlen);
        if (thisRead >= 0) {
            if (pasynTrace->getTraceMask(pasynUser) & ASYN_TRACEIO_DRIVER) {
                char inetBuff[32];
                ipAddrToDottedIP(&oa.ia, inetBuff, sizeof(inetBuff));
                asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, data, thisRead,
                          "%s (from %s) read %d\n", 
                          tty->IPDeviceName, inetBuff, thisRead);
            }
            tty->nRead += (unsigned long)thisRead;
        }
    } else {
        thisRead = recv(tty->fd, data, (int)maxchars, 0);
        if (thisRead >= 0) {
            asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, data, thisRead,
                        "%s read %d\n", tty->IPDeviceName, thisRead);
            tty->nRead += (unsigned long)thisRead;
        }
    }
    if (thisRead < 0) {
        int should_disconnect = (tty->disconnectOnReadTimeout) ||
                           ((SOCKERRNO != SOCK_EWOULDBLOCK) && (SOCKERRNO != SOCK_EINTR));
        if (should_disconnect) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                          "%s read error: %s",
                          tty->IPDeviceName, strerror(SOCKERRNO));
            closeConnection(pasynUser,tty,"Read error");
            status = asynError;
        } else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                          "%s timeout: %s",
                          tty->IPDeviceName, strerror(SOCKERRNO));
            status = asynTimeout;
        }
    }
    /* If recv() returns 0 on a SOCK_STREAM (TCP) socket, the connection has closed */
    if ((thisRead == 0) && (tty->socketType == SOCK_STREAM)) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                      "%s connection closed",
                      tty->IPDeviceName);
        closeConnection(pasynUser,tty,"Read from broken connection");
        reason |= ASYN_EOM_END;
    }
    if (thisRead < 0)
        thisRead = 0;
    *nbytesTransfered = thisRead;
    /* If there is room add a null byte */
    if (thisRead < (int) maxchars)
        data[thisRead] = 0;
    else
        reason |= ASYN_EOM_CNT;
    if (gotEom) *gotEom = reason;
    return status;
}

/*
 * Flush pending input
 */
static asynStatus
flushIt(void *drvPvt,asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int numRecv, numTotal=0;
    char cbuf[512];

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s flush\n", tty->IPDeviceName);
    if (tty->fd != INVALID_SOCKET) {
        /*
         * Toss characters until there are none left
         */
#ifndef USE_POLL
        setNonBlock(tty->fd, 1);
#endif
        while (1) {
            numRecv = recv(tty->fd, cbuf, sizeof cbuf, 0);
            if (numRecv <= 0) break;
            numTotal += numRecv;
        }
#ifndef USE_POLL
        setNonBlock(tty->fd, 0);
#endif
    }
    if (numTotal > 0) {
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, "%s flushed %d bytes\n", tty->IPDeviceName, numTotal);
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
        if (tty->fd != INVALID_SOCKET)
            epicsSocketDestroy(tty->fd);
        free(tty->portName);
        free(tty->IPDeviceName);
        free(tty);
    }
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

    assert(tty);
    if (epicsStrCaseCmp(key, "disconnectOnReadTimeout") == 0) {
        l = epicsSnprintf(val, valSize, "%c", tty->disconnectOnReadTimeout ? 'Y' : 'N');
    }
    else if (epicsStrCaseCmp(key, "hostInfo") == 0) {
        l = epicsSnprintf(val, valSize, "%s", tty->IPDeviceName);
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
setOption(void *drvPvt, asynUser *pasynUser, const char *key, const char *val)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                    "%s setOption key %s val %s\n", tty->portName, key, val);
    
    if (epicsStrCaseCmp(key, "disconnectOnReadTimeout") == 0) {
        if (epicsStrCaseCmp(val, "Y") == 0) {
            tty->disconnectOnReadTimeout = 1;
        }
        else if (epicsStrCaseCmp(val, "N") == 0) {
            tty->disconnectOnReadTimeout = 0;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                    "Invalid disconnectOnReadTimeout value.");
            return asynError;
        }
    }
    else if (epicsStrCaseCmp(key, "hostInfo") == 0) {
        int status = parseHostInfo(tty, val);
        if (status) return asynError;
    }
    else if (epicsStrCaseCmp(key, "") != 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                "Unsupported key \"%s\"", key);
        return asynError;
    }
    return asynSuccess;
}
static const struct asynOption asynOptionMethods = { setOption, getOption };

/*
 * asynCommon methods
 */
static const struct asynCommon drvAsynIPPortAsynCommon = {
    asynCommonReport,
    asynCommonConnect,
    asynCommonDisconnect
};

/*
 * Configure and register the drvAsynIPPort driver
*/
epicsShareFunc int
drvAsynIPPortConfigure(const char *portName,
                       const char *hostInfo,
                       unsigned int priority,
                       int noAutoConnect,
                       int noProcessEos)
{
    ttyController_t *tty;
    asynInterface *pasynInterface;
    asynStatus status;
    int nbytes;
    asynOctet *pasynOctet;
    static int firstTime = 1;

    /*
     * Check arguments
     */
    if (portName == NULL) {
        printf("Port name missing.\n");
        return -1;
    }
    if (hostInfo == NULL) {
        printf("TCP host information missing.\n");
        return -1;
    }
    /*
     * Perform some one-time-only initializations
     */
    if (firstTime) {
        firstTime = 0;
        if (osiSockAttach() == 0) {
            printf("drvAsynIPPortConfigure: osiSockAttach failed\n");
            return -1;
        }
    }

    /*
     * Create our private structure
     */
    nbytes = sizeof(*tty) + sizeof(asynOctet);
    tty = (ttyController_t *)callocMustSucceed(1, nbytes,
          "drvAsynIPPortConfigure()");
    pasynOctet = (asynOctet *)(tty+1);
    tty->portName = epicsStrDup(portName);
    tty->fd = INVALID_SOCKET;
    tty->isCom =  ISCOM_UNKNOWN;

    /*
     * Create socket from hostInfo
     */
    status = parseHostInfo(tty, hostInfo);
    if (status) {
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
    tty->option.interfaceType = asynOptionType;
    tty->option.pinterface  = (void *)&asynOptionMethods;
    tty->option.drvPvt = tty;
    if (pasynManager->registerPort(tty->portName,
                                   ASYN_CANBLOCK,
                                   !noAutoConnect,
                                   priority,
                                   0) != asynSuccess) {
        printf("drvAsynIPPortConfigure: Can't register myself.\n");
        ttyCleanup(tty);
        return -1;
    }
    status = pasynManager->registerInterface(tty->portName,&tty->common);
    if(status != asynSuccess) {
        printf("drvAsynIPPortConfigure: Can't register common.\n");
        ttyCleanup(tty);
        return -1;
    }
    status = pasynManager->registerInterface(tty->portName,&tty->option);
    if(status != asynSuccess) {
        printf("drvAsynIPPortConfigure: Can't register option.\n");
        ttyCleanup(tty);
        return -1;
    }
    pasynOctet->read = readIt;
    pasynOctet->write = writeIt;
    pasynOctet->flush = flushIt;
    tty->octet.interfaceType = asynOctetType;
    tty->octet.pinterface  = pasynOctet;
    tty->octet.drvPvt = tty;
    status = pasynOctetBase->initialize(tty->portName,&tty->octet, 0, 0, 1);
    if(status != asynSuccess) {
        printf("drvAsynIPPortConfigure: pasynOctetBase->initialize failed.\n");
        ttyCleanup(tty);
        return -1;
    }
    if (tty->isCom && (asynInterposeCOM(tty->portName) != 0)) {
        printf("drvAsynIPPortConfigure asynInterposeCOM failed.\n");
        return -1;
    }
    if (!noProcessEos)
        asynInterposeEosConfig(tty->portName, -1, 1, 1);
    tty->pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(tty->pasynUser,tty->portName,-1);
    if(status != asynSuccess) {
        printf("connectDevice failed %s\n",tty->pasynUser->errorMessage);
        ttyCleanup(tty);
        return -1;
    }

    /*
     * Register for socket cleanup
     */
    epicsAtExit(cleanup, tty);
    return 0;
}

/*
 * IOC shell command registration
 */
static const iocshArg drvAsynIPPortConfigureArg0 = { "port name",iocshArgString};
static const iocshArg drvAsynIPPortConfigureArg1 = { "host:port [protocol]",iocshArgString};
static const iocshArg drvAsynIPPortConfigureArg2 = { "priority",iocshArgInt};
static const iocshArg drvAsynIPPortConfigureArg3 = { "disable auto-connect",iocshArgInt};
static const iocshArg drvAsynIPPortConfigureArg4 = { "noProcessEos",iocshArgInt};
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
