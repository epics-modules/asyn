/**********************************************************************
* OSI support for interruptible system calls                          *
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
 * $Id: epicsInterruptibleSyscall.c,v 1.13 2008-05-29 14:28:55 norume Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <osiUnistd.h>
#include <osiSock.h>
#include <epicsInterruptibleSyscall.h>
#include <cantProceed.h>
#include <errlog.h>
#include <epicsAssert.h>
#include <epicsSignal.h>
#include <epicsThread.h>
#include <epicsMutex.h>

#ifdef vxWorks
# include <ioLib.h>
#else
# ifndef _WIN32
#  include <sys/ioctl.h>
# endif
#endif


struct epicsInterruptibleSyscallContext {
    int            fd;
    epicsThreadId  tid;
    epicsMutexId   mutex;
    int            interruptCount;
    int            wasClosed;
};

epicsInterruptibleSyscallContext *
epicsInterruptibleSyscallCreate(void)
{
    epicsInterruptibleSyscallContext *c;

    c = callocMustSucceed(1, sizeof *c, "epicsInterruptibleSyscallCreate");
    if (c != NULL) {
        c->fd = -1;
        c->mutex = epicsMutexMustCreate();
    }
    return c;
}

epicsInterruptibleSyscallContext *
epicsInterruptibleSyscallMustCreate(const char *msg)
{
    epicsInterruptibleSyscallContext *c = epicsInterruptibleSyscallCreate();

    if (c == NULL)
        cantProceed("%s", msg);
    return c;
}

int
epicsInterruptibleSyscallArm(epicsInterruptibleSyscallContext *c, int fd, epicsThreadId tid)
{
    epicsMutexMustLock(c->mutex);
    c->fd = fd;
    if (tid != c->tid) {
        c->tid = tid;
        epicsSignalInstallSigAlarmIgnore();
    }
    c->interruptCount = 0;
    c->wasClosed = 0;
    epicsMutexUnlock(c->mutex);
    return 0;
}

int
epicsInterruptibleSyscallDelete(epicsInterruptibleSyscallContext *c)
{
    epicsMutexMustLock(c->mutex);
    epicsMutexUnlock(c->mutex);
    epicsMutexDestroy(c->mutex);
    free(c);
    return 0;
}

int
epicsInterruptibleSyscallInterrupt(epicsInterruptibleSyscallContext *c)
{
    epicsMutexMustLock(c->mutex);
    if (++c->interruptCount == 2)
        errlogPrintf("Warning -- Multiple calls to epicsInterruptibleSyscallInterrupt().\n");
    /*
     * Force the I/O thread out of its slow system call.
     */
    if(c->fd < 0) {
        if (c->tid != NULL)
            epicsSignalRaiseSigAlarm(c->tid);
    }
    else {
        switch(epicsSocketSystemCallInterruptMechanismQuery()) {
        case esscimqi_socketCloseRequired:
            if (c->fd >= 0) {
                epicsSocketDestroy(c->fd);
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

    epicsMutexMustLock(c->mutex);
    i = (c->interruptCount > 0);
    epicsMutexUnlock(c->mutex);
    return i;
}

int
epicsInterruptibleSyscallWasClosed(epicsInterruptibleSyscallContext *c)
{
    int i;

    epicsMutexMustLock(c->mutex);
    i = c->wasClosed;
    epicsMutexUnlock(c->mutex);
    return i;
}
