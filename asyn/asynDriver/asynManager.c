/* asynManager.c */

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
#include <epicsExport.h>
#include <iocsh.h>

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
    asynUser *pasynUser;
    const char *portName;
    asynInterface *paasynInterface;
    int nasynInterface;
    const char *processModuleName;
    asynInterface *paprocessModule;
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
static asynPvt *locateAsynPvt(const char *portName);
static void deviceThread(asynPvt *pasynPvt);
    
/* forward reference to asynManager methods */
static void report(int details);
static asynUser *createAsynUser(userCallback queue, userCallback timeout);
static asynStatus freeAsynUser(asynUser *pasynUser);
static asynStatus connectPort(asynUser *pasynUser, const char *portName);
static asynStatus disconnectPort(asynUser *pasynUser);
static asynInterface *findInterface(asynUser *pasynUser,
    const char *interfaceType,int processModuleOK);
static asynStatus queueRequest(asynUser *pasynUser,
    asynQueuePriority priority,double timeout);
static void cancelRequest(asynUser *pasynUser);
static asynStatus lock(asynUser *pasynUser);
static asynStatus unlock(asynUser *pasynUser);
static asynStatus registerPort(
    const char *portName,
    asynInterface *paasynInterface,int nasynInterface,
    unsigned int priority,unsigned int stackSize);
static asynStatus registerProcessModule(
    const char *processModuleName,const char *portName,
    asynInterface *paasynInterface,int nasynInterface);


static asynManager queueManager = {
    report,
    createAsynUser,
    freeAsynUser,
    connectPort,
    disconnectPort,
    findInterface,
    queueRequest,
    cancelRequest,
    lock,
    unlock,
    registerPort,
    registerProcessModule
};
epicsShareDef asynManager *pasynManager = &queueManager;

/*internal methods */
static void asynInit(void)
{
    if(pasynBase) return;
    pasynBase = callocMustSucceed(1,sizeof(asynPvt),"asynInit");
    ellInit(&pasynBase->asynPvtList);
    pasynBase->timerQueue = epicsTimerQueueAllocate(
        1,epicsThreadPriorityScanLow);
}

static asynPvt *locateAsynPvt(const char *portName)
{
    asynPvt *pasynPvt = (asynPvt *)ellFirst(&pasynBase->asynPvtList);
    while(pasynPvt) {
        if(strcmp(portName,pasynPvt->portName)==0) return(pasynPvt);
        pasynPvt = (asynPvt *)ellNext(&pasynPvt->node);
    }
    return(0);
}

static void deviceThread(asynPvt *pasynPvt)
{
    asynUserPvt *pasynUserPvt;
    asynUser *pasynUser;
    int i;
    asynCommon *pasynCommon = 0;
    void *drvPvt = 0;

    taskwdInsert(pasynPvt->threadid,0,0);
    /* find and call pasynCommon->connect */
    for(i=0; i<pasynPvt->nasynInterface; i++) {
        asynInterface *pasynInterface = &pasynPvt->paasynInterface[i];
        if(strcmp(pasynInterface->interfaceType,asynCommonType)==0) {
            pasynCommon = (asynCommon *)pasynInterface->pinterface;
            drvPvt = pasynInterface->drvPvt;
            break;
        }
    }
    if(pasynCommon) {
        asynStatus status;
        status = pasynCommon->connect(drvPvt,pasynPvt->pasynUser);
        if(status!=asynSuccess) {
            printf("asynManager:deviceThread could not connect %s\n",
                pasynPvt->pasynUser->errorMessage);
            return;
        }
    }
    while(1) {
        if(epicsEventWait(pasynPvt->notifyDeviceThread)!=epicsEventWaitOK) {
            errlogPrintf("asynManager::deviceThread epicsEventWait error");
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
            pasynUserPvt->queueCallback(pasynUser);
        }
    }
}

/* asynManager methods */
static void report(int details)
{
    asynPvt *pasynPvt;

    if(!pasynBase) asynInit();
    pasynPvt = (asynPvt *)ellFirst(&pasynBase->asynPvtList);
    while(pasynPvt) {
	int nInQueue, i;
        asynCommon *pasynCommon;
        void *asynCommonPvt;

        pasynCommon = 0;
        epicsMutexMustLock(pasynPvt->lock);
	nInQueue = 0;
	for(i=asynQueuePriorityLow; i<=asynQueuePriorityHigh; i++) {
	    nInQueue += ellCount(&pasynPvt->queueList[i]);
	}
        epicsMutexUnlock(pasynPvt->lock);
	printf("%s thread %p priority %d queue requests %d\n",
            pasynPvt->portName,pasynPvt->threadid,pasynPvt->priority,nInQueue);
	for(i=0; i<pasynPvt->nasynInterface; i++) {
	    asynInterface *pasynInterface = &pasynPvt->paasynInterface[i];
	    printf("    %s pinterface %p drvPvt %p\n",
                pasynInterface->interfaceType,
                pasynInterface->pinterface,
                pasynInterface->drvPvt);
            if(strcmp(pasynInterface->interfaceType,asynCommonType)==0) {
                pasynCommon = pasynInterface->pinterface;
                asynCommonPvt = pasynInterface->drvPvt;
            }
	}
	if(pasynPvt->nprocessModules>0) {
	    printf("    %s is process module\n",pasynPvt->processModuleName);
	}
	for(i=0; i<pasynPvt->nprocessModules; i++) {
	    asynInterface *pasynInterface = &pasynPvt->paprocessModule[i];
	    printf("    %s pinterface %p drvPvt %p\n",
                pasynInterface->interfaceType,
                pasynInterface->pinterface,
                pasynInterface->drvPvt);
            if(strcmp(pasynInterface->interfaceType, asynCommonType)==0) {
                pasynCommon = pasynInterface->pinterface;
                asynCommonPvt = pasynInterface->drvPvt;
            }
	}
        if(pasynCommon) pasynCommon->report(asynCommonPvt,details);
        pasynPvt = (asynPvt *)ellNext(&pasynPvt->node);
    }
}

static asynUser *createAsynUser(userCallback queue, userCallback timeout)
{
    asynUserPvt *pasynUserPvt;
    asynUser *pasynUser;
    int nbytes;

    if(!pasynBase) asynInit();
    nbytes = sizeof(asynUserPvt) + ERROR_MESSAGE_SIZE;
    pasynUserPvt = callocMustSucceed(1,nbytes,"asynCommon:registerDriver");
    pasynUserPvt->queueCallback = queue;
    pasynUser = asynUserPvtToAsynUser(pasynUserPvt);
    pasynUser->errorMessage = (char *)(pasynUser +1);
    pasynUser->errorMessageSize = ERROR_MESSAGE_SIZE;
    if(timeout) {
        pasynUserPvt->timer = epicsTimerQueueCreateTimer(
            pasynBase->timerQueue,(epicsTimerCallback)timeout,pasynUser);
    }
    return(pasynUser);
}

static asynStatus freeAsynUser(asynUser *pasynUser)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);

    if(pasynUserPvt->isQueued) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:freeAsynUser asynUser is queued\n");
        return(asynError);
    }
    if(pasynUserPvt->lockCount>0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::freeAsynUser: isLocked\n");
        return(asynError);
    }
    free(pasynUserPvt);
    return(asynSuccess);
}

static asynStatus connectPort(asynUser *pasynUser, const char *portName)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynPvt *pasynPvt = locateAsynPvt(portName);

    assert(pasynUser);
    if(!pasynBase) asynInit();
    if(pasynUserPvt->pasynPvt) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:connectPort already connected to device\n");
        return(asynError);
    }
    if(!pasynPvt) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:connectPort %s not found\n",portName);
        return(asynError);
    }
    epicsMutexMustLock(pasynPvt->lock);
    pasynUserPvt->pasynPvt = pasynPvt;
    epicsMutexUnlock(pasynPvt->lock);
    return(asynSuccess);
}

static asynStatus disconnectPort(asynUser *pasynUser)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynPvt *pasynPvt = pasynUserPvt->pasynPvt;

    if(!pasynBase) asynInit();
    if(!pasynPvt) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::disconnectPort: not connected\n");
        return(asynError);
    }
    if(pasynUserPvt->isQueued) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::disconnectPort: isQueued\n");
        return(asynError);
    }
    if(pasynUserPvt->lockCount>0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::disconnectPort: isLocked\n");
        return(asynError);
    }
    epicsMutexMustLock(pasynPvt->lock);
    pasynUserPvt->pasynPvt = 0;
    epicsMutexUnlock(pasynPvt->lock);
    return(asynSuccess);
}

static asynInterface *findInterface(asynUser *pasynUser,
    const char *interfaceType,int processModuleOK)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynPvt *pasynPvt = pasynUserPvt->pasynPvt;
    asynInterface *pasynInterface;
    int i;

    if(!pasynPvt) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:findInterface: not connected\n");
        return(0);
    }
    /*Look first for processModule then for asynInterface*/
    if(processModuleOK) for(i=0; i<pasynPvt->nprocessModules; i++) {
        pasynInterface = &pasynPvt->paprocessModule[i];
        if(strcmp(interfaceType,pasynInterface->interfaceType)==0) {
            return(pasynInterface);
        }
    }
    for(i=0; i<pasynPvt->nasynInterface; i++) {
        pasynInterface = &pasynPvt->paasynInterface[i];
        if(strcmp(interfaceType,pasynInterface->interfaceType)==0) {
            return(pasynInterface);
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
                "asynManager::queueRequest not connected\n");
        return(asynError);
    }
    if(pasynUserPvt->isQueued) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::queueRequest is already queued\n");
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
                "asynManager::lock not connected\n");
        return(asynError);
    }
    if(pasynUserPvt->isQueued) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::lock is queued\n");
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
                "asynManager::unlock not connected\n");
        return(asynError);
    }
    if(pasynUserPvt->isQueued) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::unlock is queued\n");
        return(asynError);
    }
    if(pasynUserPvt->lockCount==0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::unlock but not locked\n");
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

static asynStatus registerPort(
    const char *portName,
    asynInterface *paasynInterface,int nasynInterface,
    unsigned int priority,unsigned int stackSize)
{
    asynPvt *pasynPvt;

    if(!pasynBase) asynInit();
    pasynPvt = locateAsynPvt(portName);
    if(pasynPvt) {
        printf("asynCommon:registerDriver %s already registered\n",portName);
        return(asynError);
    }
    pasynPvt = callocMustSucceed(1,sizeof(asynPvt),"asynCommon:registerDriver");
    pasynPvt->lock = epicsMutexMustCreate();
    pasynPvt->pasynUser = createAsynUser(0,0);
    pasynPvt->portName = portName;
    pasynPvt->paasynInterface = paasynInterface;
    pasynPvt->nasynInterface = nasynInterface;
    pasynPvt->notifyDeviceThread = epicsEventMustCreate(epicsEventEmpty);
    pasynPvt->priority = priority;
    pasynPvt->stackSize = stackSize;
    pasynPvt->threadid = epicsThreadCreate(portName,priority,stackSize,
        (EPICSTHREADFUNC)deviceThread,pasynPvt);
    if(!pasynPvt->threadid){
        printf("asynCommon:registerDriver %s epicsThreadCreate failed \n",
            portName);
        return(asynError);
    }
    ellAdd(&pasynBase->asynPvtList,&pasynPvt->node);
    return(asynSuccess);
}

static asynStatus registerProcessModule(
    const char *processModuleName,const char *portName,
    asynInterface *paasynInterface,int nasynInterface)
{
    asynPvt *pasynPvt;

    if(!pasynBase) asynInit();
    pasynPvt = locateAsynPvt(portName);
    if(!pasynPvt) {
        printf("asynCommon:registerProcessModule %s not found\n",portName);
        return(asynError);
    }
    if(pasynPvt->nprocessModules>0) {
        printf("asynCommon:registerProcessModule %s already "
		"has a process module registered\n",portName);
        return(asynError);
    }
    pasynPvt->processModuleName = processModuleName;
    pasynPvt->paprocessModule = paasynInterface;
    pasynPvt->nprocessModules = nasynInterface;
    return(asynSuccess);
}

static const iocshArg asynReportArg0 = {"level", iocshArgInt};
static const iocshArg *const asynReportArgs[1] = {&asynReportArg0};
static const iocshFuncDef asynReportDef =
    {"asynReport", 1, asynReportArgs};
static void asynReportCall(const iocshArgBuf * args) {
        report(args[0].ival);
}
static void asyn(void)
{
    static int firstTime = 1;
    if(!firstTime) return;
    firstTime = 0;
    iocshRegister(&asynReportDef,asynReportCall);
}
epicsExportRegistrar(asyn);
