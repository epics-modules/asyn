/* asynDriver.c */

/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* gpibCore is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/* Generic asynchronous driver
 *
 * Author: Marty Kraimer
 */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <ellLib.h>
#include <errlog.h>
#include <taskwd.h>
#include <epicsStdio.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <epicsThread.h>
#include <epicsTime.h>
#include <epicsTimer.h>
#include <cantProceed.h>
#include <epicsAssert.h>

#define epicsExportSharedSymbols
#include "asynDriver.h"

#define BOOL int
#define TRUE 1
#define FALSE 0
#define ERROR_MESSAGE_SIZE 160
#define NUMBER_QUEUE_PRIORITIES (asynQueuePriorityHigh + 1)

typedef struct asynBase {
    ELLLIST asynPvtList;
    epicsTimerQueueId timerQueue;
}asynBase;

static asynBase *pasynBase = 0;

typedef struct asynUserPvt {
    ELLNODE      node;
    userCallback queueCallback;
    userCallback timeoutCallback;
    BOOL         isQueued;
    unsigned int lockCount;
    epicsTimerId timer;
    double       timeout;
    asynUser     user;
}asynUserPvt;
#define asynUserPvtToAsynUser(p) (&p->user)
#define asynUserToAsynUserPvt(p) \
  ((asynUserPvt *) ((char *)(p) \
          - ( (char *)&(((asynUserPvt *)0)->user) - (char *)0 ) ) )

struct asynPvt {
    ELLNODE node;
    epicsMutexId lock;
    drvPvt *pdrvPvt;
    const char *name;
    driverInfo *padriverInfo;
    int ndriverTypes;
    epicsEventId notifyDeviceThread;
    unsigned int priority;
    unsigned int stackSize;
    epicsThreadId threadid;
    ELLLIST queueList[NUMBER_QUEUE_PRIORITIES];
    asynUserPvt *plockHolder;
};

/* forward reference to internal methods */
static void asynInit(void);
static asynPvt *locateAsynPvt(const char *name);
static void deviceThread(asynPvt *pasynPvt);
    
/* forward reference to asynQueueManager methods */
static void report(int details);
static asynUser *createAsynUser(
    userCallback queue, userCallback timeout,userPvt *puserPvt);
static asynStatus freeAsynUser(asynUser *pasynUser);
static asynStatus connectDevice(asynUser *pasynUser, const char *name);
static asynStatus disconnectDevice(asynUser *pasynUser);
static void *findDriver(asynUser *pasynUser,const char *driverType);
static asynStatus queueRequest(asynUser *pasynUser,
    asynQueuePriority priority,double timeout);
static asynCancelStatus cancelRequest(asynUser *pasynUser);
static asynStatus lock(asynUser *pasynUser);
static asynStatus unlock(asynUser *pasynUser);
static asynPvt *registerDriver(drvPvt *pdrvPvt, const char *name,
    driverInfo *padriverInfo,int ndriverTypes,
    unsigned int priority,unsigned int stackSize);

static asynQueueManager queueManager = {
    report,
    createAsynUser,
    freeAsynUser,
    connectDevice,
    disconnectDevice,
    findDriver,
    queueRequest,
    cancelRequest,
    lock,
    unlock,
    registerDriver
};
epicsShareDef asynQueueManager *pasynQueueManager = &queueManager;

/*internal methods */
void asynInit(void)
{
    if(pasynBase) return;
    pasynBase = callocMustSucceed(1,sizeof(asynPvt),"asynInit");
    ellInit(&pasynBase->asynPvtList);
    pasynBase-> timerQueue = epicsTimerQueueAllocate(
        1,epicsThreadPriorityScanLow);
}

static asynPvt *locateAsynPvt(const char *name)
{
    asynPvt *pasynPvt = (asynPvt *)ellFirst(&pasynBase->asynPvtList);
    while(pasynPvt) {
        if(strcmp(name,pasynPvt->name)==0) return(pasynPvt);
        pasynPvt = (asynPvt *)ellNext(&pasynPvt->node);
    }
    return(0);
}

static void deviceThread(asynPvt *pasynPvt)
{
    asynUserPvt *pasynUserPvt;
    asynUser *pasynUser;
    int i;

    taskwdInsert(pasynPvt->threadid,0,0);
    while(1) {
        if(epicsEventWait(pasynPvt->notifyDeviceThread)!=epicsEventWaitOK) {
            errlogPrintf("asynQueueManager::deviceThread epicsEventWait error");
        }
        while(1) {
            epicsMutexMustLock(pasynPvt->lock);
            for(i=asynQueuePriorityHigh; i>=asynQueuePriorityLow; i--) {
                pasynUserPvt = (asynUserPvt *)ellFirst(&pasynPvt->queueList[i]);
                while(pasynUserPvt){
                    if(!pasynPvt->plockHolder
                    || pasynPvt->plockHolder==pasynUserPvt) {
                        ellDelete(&pasynPvt->queueList[i],&pasynUserPvt->node);
                        pasynUserPvt->isQueued = FALSE;
                        break;
                    }
                    pasynUserPvt = (asynUserPvt *)ellNext(&pasynUserPvt->node);
                }
                if(pasynUserPvt) break;
            }
            if(!pasynUserPvt) {
                epicsMutexUnlock(pasynPvt->lock);
                break;
            }
            pasynUser = asynUserPvtToAsynUser(pasynUserPvt);
            if(pasynUserPvt->lockCount>0) {
                pasynPvt->plockHolder = pasynUserPvt;
            }
            if(pasynUserPvt->timer && pasynUserPvt->timeout>0.0) {
                epicsTimerCancel(pasynUserPvt->timer);
            }
            epicsMutexUnlock(pasynPvt->lock);
            pasynUserPvt->queueCallback(pasynUser->puserPvt);
        }
    }
}

/* asynQueueManager methods */
static void report(int details)
{
    asynPvt *pasynPvt;

    if(!pasynBase) asynInit();
    pasynPvt = (asynPvt *)ellFirst(&pasynBase->asynPvtList);
    while(pasynPvt) {
	int nInQueue, i;
        epicsMutexMustLock(pasynPvt->lock);
	nInQueue = 0;
	for(i=asynQueuePriorityLow; i<=asynQueuePriorityHigh; i++) {
	    nInQueue += ellCount(&pasynPvt->queueList[i]);
	}
        epicsMutexUnlock(pasynPvt->lock);
	printf("%s thread %p priority %d queue requests %d\n",
            pasynPvt->name,pasynPvt->threadid,pasynPvt->priority,nInQueue);
	for(i=0; i<pasynPvt->ndriverTypes; i++) {
	    driverInfo *pdriverInfo = &pasynPvt->padriverInfo[i];
	    printf("    %s %p\n",
                pdriverInfo->driverType, pdriverInfo->pinterface);
	}
        pasynPvt = (asynPvt *)ellNext(&pasynPvt->node);
    }
}

static asynUser *createAsynUser(
    userCallback queue, userCallback timeout,userPvt *puserPvt)
{
    asynUserPvt *pasynUserPvt;
    asynUser *pasynUser;
    int nbytes;

    if(!pasynBase) asynInit();
    nbytes = sizeof(asynUserPvt) + ERROR_MESSAGE_SIZE;
    pasynUserPvt = callocMustSucceed(1,nbytes,"asynDriver:registerDriver");
    pasynUserPvt->queueCallback = queue;
    pasynUserPvt->timeoutCallback = timeout;
    if(timeout) {
        pasynUserPvt->timer = epicsTimerQueueCreateTimer(
            pasynBase->timerQueue,(epicsTimerCallback)timeout,puserPvt);
    }
    pasynUser = asynUserPvtToAsynUser(pasynUserPvt);
    pasynUser->errorMessage = (char *)(pasynUser +1);
    pasynUser->errorMessageSize = ERROR_MESSAGE_SIZE;
    pasynUser->puserPvt = puserPvt;
    return(pasynUser);
}

static asynStatus freeAsynUser(asynUser *pasynUser)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);

    if(pasynUserPvt->isQueued) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager:freeAsynUser asynUser is queued\n");
        return(asynError);
    }
    if(pasynUserPvt->lockCount>0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager::freeAsynUser: isLocked\n");
        return(asynError);
    }
    if(pasynUser->pdrvPvt) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager:freeAsynUser connected to driver\n");
        return(asynError);
    }
    free(pasynUserPvt);
    return(asynSuccess);
}

static asynStatus connectDevice(asynUser *pasynUser, const char *name)
{
    asynPvt *pasynPvt = locateAsynPvt(name);

    assert(pasynUser);
    if(!pasynBase) asynInit();
    if(pasynUser->pasynPvt) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager:connectDevice already connected to device\n");
        return(asynError);
    }
    if(!pasynPvt) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager:connectDevice %s not found\n",name);
        return(asynError);
    }
    epicsMutexMustLock(pasynPvt->lock);
    pasynUser->pasynPvt = pasynPvt;
    pasynUser->pdrvPvt = pasynPvt->pdrvPvt;
    epicsMutexUnlock(pasynPvt->lock);
    return(asynSuccess);
}

static asynStatus disconnectDevice(asynUser *pasynUser)
{
    asynPvt *pasynPvt = pasynUser->pasynPvt;
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);

    if(!pasynBase) asynInit();
    if(!pasynPvt) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager::disconnectDevice: not connected\n");
        return(asynError);
    }
    if(pasynUserPvt->isQueued) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager::disconnectDevice: isQueued\n");
        return(asynError);
    }
    if(pasynUserPvt->lockCount>0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager::disconnectDevice: isLocked\n");
        return(asynError);
    }
    epicsMutexMustLock(pasynPvt->lock);
    pasynUser->pasynPvt = 0;
    pasynUser->pdrvPvt = 0;
    epicsMutexUnlock(pasynPvt->lock);
    return(asynSuccess);
}

static void *findDriver(asynUser *pasynUser,const char *driverType)
{
    asynPvt *pasynPvt = pasynUser->pasynPvt;
    driverInfo *pdriverInfo;
    int i;

    if(!pasynPvt) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager:findDriver: not connected\n");
        return(0);
    }
    for(i=0; i<pasynPvt->ndriverTypes; i++) {
        pdriverInfo = &pasynPvt->padriverInfo[i];
        if(strcmp(driverType,pdriverInfo->driverType)==0) {
            return(pdriverInfo->pinterface);
        }
    }
    return(0);
}

static asynStatus queueRequest(asynUser *pasynUser,
    asynQueuePriority priority,double timeout)
{
    asynPvt *pasynPvt = pasynUser->pasynPvt;
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);

    assert(priority>=asynQueuePriorityLow && priority<=asynQueuePriorityHigh);
    if(!pasynPvt) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager::queueRequest not connected\n");
        return(asynError);
    }
    if(pasynUserPvt->isQueued) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager::queueRequest is already queued\n");
        return(asynError);
    }
    epicsMutexMustLock(pasynPvt->lock);
    if(pasynPvt->plockHolder && pasynPvt->plockHolder==pasynUserPvt) {
        ellInsert(&pasynPvt->queueList[priority],0,&pasynUserPvt->node);
    } else {
        ellAdd(&pasynPvt->queueList[priority],&pasynUserPvt->node);
    }
    pasynUserPvt->isQueued = TRUE;
    pasynUserPvt->timeout = timeout;
    if(pasynUserPvt->timer && pasynUserPvt->timeout>0.0) {
         epicsTimerStartDelay(pasynUserPvt->timer,pasynUserPvt->timeout);
    }
    epicsMutexUnlock(pasynPvt->lock);
    epicsEventSignal(pasynPvt->notifyDeviceThread);
    return(asynSuccess);
}

static asynCancelStatus cancelRequest(asynUser *pasynUser)
{
    asynPvt *pasynPvt = pasynUser->pasynPvt;
    asynUserPvt *pasynUserPvt;
    int i;

    epicsMutexMustLock(pasynPvt->lock);
    for(i=asynQueuePriorityHigh; i>=asynQueuePriorityLow; i--) {
        pasynUserPvt = (asynUserPvt *)ellFirst(&pasynPvt->queueList[i]);
	while(pasynUserPvt) {
	    if(pasynUser == &pasynUserPvt->user) {
	        ellDelete(&pasynPvt->queueList[i],&pasynUserPvt->node);
                pasynUserPvt->isQueued = FALSE;
                if(pasynUserPvt->timer && pasynUserPvt->timeout>0.0) {
                    epicsTimerCancel(pasynUserPvt->timer);
                }
	        break;
	    }
	    pasynUserPvt = (asynUserPvt *)ellNext(&pasynUserPvt->node);
	}
	if(pasynUserPvt) break;
    }
    epicsMutexUnlock(pasynPvt->lock);
    if(!pasynUserPvt) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager:cancelRequest: not queued\n");
        return(asynError);
    }
    return(asynSuccess);
}

static asynStatus lock(asynUser *pasynUser)
{
    asynPvt *pasynPvt = pasynUser->pasynPvt;
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);

    if(!pasynPvt) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager::lock not connected\n");
        return(asynError);
    }
    if(pasynUserPvt->isQueued) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager::lock is queued\n");
        return(asynError);
    }
    epicsMutexMustLock(pasynPvt->lock);
    pasynUserPvt->lockCount++;
    epicsMutexUnlock(pasynPvt->lock);
    return(asynSuccess);
}

static asynStatus unlock(asynUser *pasynUser)
{
    asynPvt *pasynPvt = pasynUser->pasynPvt;
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    BOOL wasOwner = FALSE;

    if(!pasynPvt) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager::unlock not connected\n");
        return(asynError);
    }
    if(pasynUserPvt->isQueued) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager::unlock is queued\n");
        return(asynError);
    }
    if(pasynUserPvt->lockCount==0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager::unlock but not locked\n");
        return(asynError);
    }
    epicsMutexMustLock(pasynPvt->lock);
    pasynUserPvt->lockCount--;
    if(pasynPvt->plockHolder==pasynUserPvt) {
        pasynPvt->plockHolder = 0;
        wasOwner = TRUE;
    }
    epicsMutexUnlock(pasynPvt->lock);
    if(wasOwner) epicsEventSignal(pasynPvt->notifyDeviceThread);
    return(asynSuccess);
}

static asynPvt *registerDriver(drvPvt *pdrvPvt, const char *name,
    driverInfo *padriverInfo,int ndriverTypes,
    unsigned int priority,unsigned int stackSize)
{
    asynPvt *pasynPvt;

    if(!pasynBase) asynInit();
    pasynPvt = locateAsynPvt(name);
    if(pasynPvt) {
        printf("asynDriver:registerDriver %s already registered\n",name);
        return(0);
    }
    pasynPvt = callocMustSucceed(1,sizeof(asynPvt),"asynDriver:registerDriver");
    pasynPvt->lock = epicsMutexMustCreate();
    pasynPvt->pdrvPvt = pdrvPvt;
    pasynPvt->name = name;
    pasynPvt->padriverInfo = padriverInfo;
    pasynPvt->ndriverTypes = ndriverTypes;
    pasynPvt->notifyDeviceThread = epicsEventMustCreate(epicsEventEmpty);
    pasynPvt->priority = priority;
    pasynPvt->stackSize = stackSize;
    pasynPvt->threadid = epicsThreadCreate(name,priority,stackSize,
        (EPICSTHREADFUNC)deviceThread,pasynPvt);
    if(!pasynPvt->threadid){
        printf("asynDriver:registerDriver %s epicsThreadCreate failed \n",name);
        return(0);
    }
    ellAdd(&pasynBase->asynPvtList,&pasynPvt->node);
    return(pasynPvt);
}
