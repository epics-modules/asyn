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

typedef struct asynUserPvt asynUserPvt;
typedef struct asynPvt {
    ELLNODE node;
    epicsMutexId lock;
    const char *deviceName;
    deviceDriver *padeviceDriver;
    int ndeviceDrivers;
    const char *processModuleName;
    deviceDriver *paprocessModule;
    int nprocessModules;
    epicsEventId notifyDeviceThread;
    unsigned int priority;
    unsigned int stackSize;
    epicsThreadId threadid;
    ELLLIST queueList[NUMBER_QUEUE_PRIORITIES];
    asynUserPvt *plockHolder;
}asynPvt;

struct asynUserPvt {
    ELLNODE      node;
    userCallback queueCallback;
    userCallback timeoutCallback;
    BOOL         isQueued;
    unsigned int lockCount;
    epicsTimerId timer;
    double       timeout; /*For queueRequest*/
    asynPvt      *pasynPvt;
    asynUser     user;
};
#define asynUserPvtToAsynUser(p) (&p->user)
#define asynUserToAsynUserPvt(p) \
  ((asynUserPvt *) ((char *)(p) \
          - ( (char *)&(((asynUserPvt *)0)->user) - (char *)0 ) ) )

/* forward reference to internal methods */
static void asynInit(void);
static asynPvt *locateAsynPvt(const char *deviceName);
static void deviceThread(asynPvt *pasynPvt);
    
/* forward reference to asynQueueManager methods */
static void report(int details);
static asynUser *createAsynUser(
    userCallback queue, userCallback timeout,void *puserPvt);
static asynStatus freeAsynUser(asynUser *pasynUser);
static asynStatus connectDevice(asynUser *pasynUser, const char *deviceName);
static asynStatus disconnectDevice(asynUser *pasynUser);
static deviceDriver *findDriver(asynUser *pasynUser,
    const char *driverType,int processModuleOK);
static asynStatus queueRequest(asynUser *pasynUser,
    asynQueuePriority priority,double timeout);
static void cancelRequest(asynUser *pasynUser);
static asynStatus lock(asynUser *pasynUser);
static asynStatus unlock(asynUser *pasynUser);
static asynStatus registerDevice(
    const char *deviceName,
    deviceDriver *padeviceDriver,int ndeviceDrivers,
    unsigned int priority,unsigned int stackSize);
static asynStatus registerProcessModule(
    const char *processModuleName,const char *deviceName,
    deviceDriver *padeviceDriver,int ndeviceDrivers);


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
    registerDevice,
    registerProcessModule
};
epicsShareDef asynQueueManager *pasynQueueManager = &queueManager;

/*internal methods */
static void asynInit(void)
{
    if(pasynBase) return;
    pasynBase = callocMustSucceed(1,sizeof(asynPvt),"asynInit");
    ellInit(&pasynBase->asynPvtList);
    pasynBase->timerQueue = epicsTimerQueueAllocate(
        1,epicsThreadPriorityScanLow);
}

static asynPvt *locateAsynPvt(const char *deviceName)
{
    asynPvt *pasynPvt = (asynPvt *)ellFirst(&pasynBase->asynPvtList);
    while(pasynPvt) {
        if(strcmp(deviceName,pasynPvt->deviceName)==0) return(pasynPvt);
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
            pasynPvt->deviceName,pasynPvt->threadid,pasynPvt->priority,nInQueue);
	for(i=0; i<pasynPvt->ndeviceDrivers; i++) {
	    deviceDriver *pdeviceDriver = &pasynPvt->padeviceDriver[i];
	    printf("    %s pinterface %p pdrvPvt %p\n",
                pdeviceDriver->pdriverInterface->driverType,
                pdeviceDriver->pdriverInterface->pinterface,
                pdeviceDriver->pdrvPvt);
	}
	if(pasynPvt->nprocessModules>0) {
	    printf("    %s is process module\n",pasynPvt->processModuleName);
	}
	for(i=0; i<pasynPvt->nprocessModules; i++) {
	    deviceDriver *pdeviceDriver = &pasynPvt->paprocessModule[i];
	    printf("    %s pinterface %p pdrvPvt %p\n",
                pdeviceDriver->pdriverInterface->driverType,
                pdeviceDriver->pdriverInterface->pinterface,
                pdeviceDriver->pdrvPvt);
	}
        pasynPvt = (asynPvt *)ellNext(&pasynPvt->node);
    }
}

static asynUser *createAsynUser(
    userCallback queue, userCallback timeout,void *puserPvt)
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
    free(pasynUserPvt);
    return(asynSuccess);
}

static asynStatus connectDevice(asynUser *pasynUser, const char *deviceName)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynPvt *pasynPvt = locateAsynPvt(deviceName);

    assert(pasynUser);
    if(!pasynBase) asynInit();
    if(pasynUserPvt->pasynPvt) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager:connectDevice already connected to device\n");
        return(asynError);
    }
    if(!pasynPvt) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager:connectDevice %s not found\n",deviceName);
        return(asynError);
    }
    epicsMutexMustLock(pasynPvt->lock);
    pasynUserPvt->pasynPvt = pasynPvt;
    epicsMutexUnlock(pasynPvt->lock);
    return(asynSuccess);
}

static asynStatus disconnectDevice(asynUser *pasynUser)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynPvt *pasynPvt = pasynUserPvt->pasynPvt;

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
    pasynUserPvt->pasynPvt = 0;
    epicsMutexUnlock(pasynPvt->lock);
    return(asynSuccess);
}

static deviceDriver *findDriver(asynUser *pasynUser,
    const char *driverType,int processModuleOK)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynPvt *pasynPvt = pasynUserPvt->pasynPvt;
    deviceDriver *pdeviceDriver;
    int i;

    if(!pasynPvt) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynQueueManager:findDriver: not connected\n");
        return(0);
    }
    /*Look first for processModule then for deviceDriver*/
    if(processModuleOK) for(i=0; i<pasynPvt->nprocessModules; i++) {
        pdeviceDriver = &pasynPvt->paprocessModule[i];
        if(strcmp(driverType,pdeviceDriver->pdriverInterface->driverType)==0) {
            return(pdeviceDriver);
        }
    }
    for(i=0; i<pasynPvt->ndeviceDrivers; i++) {
        pdeviceDriver = &pasynPvt->padeviceDriver[i];
        if(strcmp(driverType,pdeviceDriver->pdriverInterface->driverType)==0) {
            return(pdeviceDriver);
        }
    }
    return(0);
}

static asynStatus queueRequest(asynUser *pasynUser,
    asynQueuePriority priority,double timeout)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynPvt *pasynPvt = pasynUserPvt->pasynPvt;

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

static void cancelRequest(asynUser *pasynUser)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynPvt *pasynPvt = pasynUserPvt->pasynPvt;
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
}

static asynStatus lock(asynUser *pasynUser)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynPvt *pasynPvt = pasynUserPvt->pasynPvt;

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
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynPvt *pasynPvt = pasynUserPvt->pasynPvt;
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

static asynStatus registerDevice(
    const char *deviceName,
    deviceDriver *padeviceDriver,int ndeviceDrivers,
    unsigned int priority,unsigned int stackSize)
{
    asynPvt *pasynPvt;

    if(!pasynBase) asynInit();
    pasynPvt = locateAsynPvt(deviceName);
    if(pasynPvt) {
        printf("asynDriver:registerDriver %s already registered\n",deviceName);
        return(asynError);
    }
    pasynPvt = callocMustSucceed(1,sizeof(asynPvt),"asynDriver:registerDriver");
    pasynPvt->lock = epicsMutexMustCreate();
    pasynPvt->deviceName = deviceName;
    pasynPvt->padeviceDriver = padeviceDriver;
    pasynPvt->ndeviceDrivers = ndeviceDrivers;
    pasynPvt->notifyDeviceThread = epicsEventMustCreate(epicsEventEmpty);
    pasynPvt->priority = priority;
    pasynPvt->stackSize = stackSize;
    pasynPvt->threadid = epicsThreadCreate(deviceName,priority,stackSize,
        (EPICSTHREADFUNC)deviceThread,pasynPvt);
    if(!pasynPvt->threadid){
        printf("asynDriver:registerDriver %s epicsThreadCreate failed \n",
            deviceName);
        return(asynError);
    }
    ellAdd(&pasynBase->asynPvtList,&pasynPvt->node);
    return(asynSuccess);
}

static asynStatus registerProcessModule(
    const char *processModuleName,const char *deviceName,
    deviceDriver *padeviceDriver,int ndeviceDrivers)
{
    asynPvt *pasynPvt;

    if(!pasynBase) asynInit();
    pasynPvt = locateAsynPvt(deviceName);
    if(!pasynPvt) {
        printf("asynDriver:registerProcessModule %s not found\n",deviceName);
        return(asynError);
    }
    if(pasynPvt->nprocessModules>0) {
        printf("asynDriver:registerProcessModule %s already "
		"has a process module registered\n",deviceName);
        return(asynError);
    }
    pasynPvt->processModuleName = processModuleName;
    pasynPvt->paprocessModule = padeviceDriver;
    pasynPvt->nprocessModules = ndeviceDrivers;
    return(asynSuccess);
}
