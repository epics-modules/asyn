/**********************************************************************
* OSI support for interruptible system calls                          *
**********************************************************************/       
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* EPICS BASE Versions 3.13.7 and higher are distributed subject to a
* Software License Agreement found in file LICENSE that is included
* with this distribution.
***********************************************************************/

/*
 * $Id: epicsInterruptibleSyscall.c,v 1.4 2003-11-07 23:08:26 norume Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <osiSock.h>
#include <epicsInterruptibleSyscall.h>
#include <cantProceed.h>
#include <errlog.h>
#include <epicsAssert.h>
#include <epicsSignal.h>
#include <epicsThread.h>
#include <epicsMutex.h>

#ifdef vxWorks
# include <tyLib.h>
# include <ioLib.h>
#else
# include <termios.h>
#endif

struct epicsInterruptibleSyscallContext {
    int            fd;
    epicsThreadId  tid;
    epicsMutexId   mutex;
    int            interruptCount;
    int            isatty;
    int            wasClosed;
};

static epicsThreadOnceId installOnce = EPICS_THREAD_ONCE_INIT;
static void
installSigAlarmIgnore(void *p)
{
    epicsSignalInstallSigAlarmIgnore();
}

epicsInterruptibleSyscallContext *
epicsInterruptibleSyscallCreate(void)
{
    epicsInterruptibleSyscallContext *c;

    c = calloc(1, sizeof *c);
    if (c != NULL) {
        c->fd = -1;
        c->mutex = epicsMutexMustCreate();
        epicsThreadOnce(&installOnce, installSigAlarmIgnore, NULL);
    }
    return c;
}

epicsInterruptibleSyscallContext *
epicsInterruptibleSyscallMustCreate(const char *msg)
{
    epicsInterruptibleSyscallContext *c = epicsInterruptibleSyscallCreate();

    if (c == NULL)
        cantProceed(msg);
    return c;
}

static void
resetInterruptibleSyscall(epicsInterruptibleSyscallContext *c)
{
}

int
epicsInterruptibleSyscallArm(epicsInterruptibleSyscallContext *c, int fd, epicsThreadId tid)
{
    epicsMutexLock(c->mutex);
    c->fd = fd;
    c->tid = tid;
    c->interruptCount = 0;
    if(c->fd >= 0)
        c->isatty = isatty(c->fd);
    c->wasClosed = 0;
    epicsMutexUnlock(c->mutex);
    return 0;
}

int
epicsInterruptibleSyscallDelete(epicsInterruptibleSyscallContext *c)
{
    epicsMutexLock(c->mutex);
    epicsMutexUnlock(c->mutex);
    epicsMutexDestroy(c->mutex);
    free(c);
    return 0;
}

int
epicsInterruptibleSyscallInterrupt(epicsInterruptibleSyscallContext *c)
{
    epicsMutexLock(c->mutex);
    if (++c->interruptCount == 2)
        errlogPrintf("Warning -- Multiple calls to epicsInterruptibleSyscallInterrupt().\n");
    /*
     * Force the I/O thread out of its slow system call.
     */
    if(c->fd < 0) {
        if (c->tid != NULL)
            epicsSignalRaiseSigAlarm(c->tid);
    }
    else if(c->isatty) {
#ifdef vxWorks
        ioctl(c->fd, FIOCANCEL, 0);
#else
        tcflush(c->fd, TCOFLUSH);
        if (c->tid != NULL)
            epicsSignalRaiseSigAlarm(c->tid);
#endif
    }
    else {  /* Assume it's a socket */
        switch(epicsSocketSystemCallInterruptMechanismQuery()) {
        case esscimqi_socketCloseRequired:
            if (c->fd >= 0) {
                close(c->fd);
                c->wasClosed = 1;
                c->fd = -1;
            }
            break;

        case esscimqi_socketBothShutdownRequired:
            shutdown(c->fd, SHUT_RDWR);
            break;

        case esscimqi_socketSigAlarmRequired:
            if (c->tid != NULL)
                epicsSignalRaiseSigAlarm(c->tid);
            break;

        default:
            errlogPrintf("No mechanism for unblocking socket I/O!\n");
            break;
        }
    }
    epicsMutexUnlock(c->mutex);
    return 0;
}

int
epicsInterruptibleSyscallWasInterrupted(epicsInterruptibleSyscallContext *c)
{
    int i;

    epicsMutexLock(c->mutex);
    i = (c->interruptCount > 0);
    epicsMutexUnlock(c->mutex);
    return i;
}

int
epicsInterruptibleSyscallWasClosed(epicsInterruptibleSyscallContext *c)
{
    int i;

    epicsMutexLock(c->mutex);
    i = c->wasClosed;
    epicsMutexUnlock(c->mutex);
    return i;
}
