/* asynManager.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/* Author: Marty Kraimer */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>

#include <ellLib.h>
#include <errlog.h>
#include <taskwd.h>
#include <epicsStdio.h>
#include <epicsString.h>
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
#define DEFAULT_TRACE_TRUNCATE_SIZE 80

typedef struct asynBase {
    ELLLIST asynPortList;
    epicsTimerQueueId timerQueue;
    epicsMutexId  lockTrace;
}asynBase;
static asynBase *pasynBase = 0;

typedef struct asynUserPvt asynUserPvt;

typedef struct asynTracePvt {
    int           traceMask;
    int           traceIOMask;
    FILE          *fd;
    int           traceTruncateSize;
    int           traceBufferSize;
    char          *traceBuffer;
}asynTracePvt;

typedef struct asynDevice {
    ELLNODE       node;     /*For asynPort.asynDeviceList*/
    int           addr;
    const char    *processModuleName;
    asynInterface *paprocessModule;
    int           nprocessModules;
    asynUserPvt   *plockHolder;
    asynTracePvt  trace;
}asynDevice;

typedef struct asynPort {
    ELLNODE       node;  /*For asynBase.asynPortList*/
    epicsMutexId  lock;
    asynUser      *pasynUser;
    const char    *portName;
    asynInterface *paasynInterface;
    int           nasynInterface;
    epicsEventId  notifyDeviceThread;
    unsigned int  priority;
    unsigned int  stackSize;
    epicsThreadId threadid;
    ELLLIST       queueList[NUMBER_QUEUE_PRIORITIES];
    ELLLIST       asynDeviceList;
}asynPort;

struct asynUserPvt {
    ELLNODE      node;        /*For asynPort.queueList*/
    userCallback queueCallback;
    userCallback timeoutCallback;
    BOOL         isQueued;
    unsigned int lockCount;
    epicsTimerId timer;
    double       timeout; /*For queueRequest*/
    asynPort     *pasynPort;
    asynDevice   *pasynDevice;
    asynUser     user;
};

#define asynUserPvtToAsynUser(p) (&p->user)
#define asynUserToAsynUserPvt(p) \
  ((asynUserPvt *) ((char *)(p) \
          - ( (char *)&(((asynUserPvt *)0)->user) - (char *)0 ) ) )

/* forward reference to internal methods */
static void asynInit(void);
static asynPort *locateAsynPort(const char *portName);
static asynDevice *locateAsynDevice(asynPort *pasynPort, int addr);
static void queueTimeoutCallback(void *);
static void portThread(asynPort *pasynPort);
    
/* forward reference to asynManager methods */
static void report(FILE *fd,int details);
static asynUser *createAsynUser(userCallback queue, userCallback timeout);
static asynStatus freeAsynUser(asynUser *pasynUser);
static asynStatus connectDevice(asynUser *pasynUser,
    const char *portName,int addr);
static asynStatus disconnectDevice(asynUser *pasynUser);
static asynInterface *findInterface(asynUser *pasynUser,
    const char *interfaceType,int processModuleOK);
static asynInterface *findPortInterface(const char *portName,
    const char *interfaceType);
static asynStatus queueRequest(asynUser *pasynUser,
    asynQueuePriority priority,double timeout);
static int cancelRequest(asynUser *pasynUser);
static asynStatus lock(asynUser *pasynUser);
static asynStatus unlock(asynUser *pasynUser);
static int getAddr(asynUser *pasynUser);

static asynStatus registerPort(
    const char *portName,
    asynInterface *paasynInterface,int nasynInterface,
    unsigned int priority,unsigned int stackSize);
static asynStatus registerProcessModule(
    const char *processModuleName,const char *portName,int addr,
    asynInterface *paasynInterface,int nasynInterface);


static asynManager queueManager = {
    report,
    createAsynUser,
    freeAsynUser,
    connectDevice,
    disconnectDevice,
    findInterface,
    findPortInterface,
    queueRequest,
    cancelRequest,
    lock,
    unlock,
    getAddr,
    registerPort,
    registerProcessModule
};
epicsShareDef asynManager *pasynManager = &queueManager;

static asynStatus traceLock(asynUser *pasynUser);
static asynStatus traceUnlock(asynUser *pasynUser);
static asynStatus setTraceMask(asynUser *pasynUser,int mask);
static int        getTraceMask(asynUser *pasynUser);
static asynStatus setTraceIOMask(asynUser *pasynUser,int mask);
static int        getTraceIOMask(asynUser *pasynUser);
static asynStatus setTraceFILE(asynUser *pasynUser,FILE *fd);
static FILE       *getTraceFILE(asynUser *pasynUser);
static asynStatus setTraceIOTruncateSize(asynUser *pasynUser,int size);
static int        getTraceIOTruncateSize(asynUser *pasynUser);
static int        tracePrint(asynUser *pasynUser,
                      int reason, const char *pformat, ...);
static int        tracePrintIO(asynUser *pasynUser,int reason,
                      const char *buffer, int len,const char *pformat, ...);
static asynTrace asynTraceManager = {
    traceLock,
    traceUnlock,
    setTraceMask,
    getTraceMask,
    setTraceIOMask,
    getTraceIOMask,
    setTraceFILE,
    getTraceFILE,
    setTraceIOTruncateSize,
    getTraceIOTruncateSize,
    tracePrint,
    tracePrintIO
};
epicsShareDef asynTrace *pasynTrace = &asynTraceManager;


/*internal methods */
static void asynInit(void)
{
    if(pasynBase) return;
    pasynBase = callocMustSucceed(1,sizeof(asynPort),"asynInit");
    ellInit(&pasynBase->asynPortList);
    pasynBase->timerQueue = epicsTimerQueueAllocate(
        1,epicsThreadPriorityScanLow);
    pasynBase->lockTrace = epicsMutexMustCreate();
}

/*locateAsynPort returns 0 if portName is not registered*/
static asynPort *locateAsynPort(const char *portName)
{
    asynPort *pasynPort = (asynPort *)ellFirst(&pasynBase->asynPortList);
    while(pasynPort) {
        if(strcmp(portName,pasynPort->portName)==0) return(pasynPort);
        pasynPort = (asynPort *)ellNext(&pasynPort->node);
    }
    return(0);
}

/*locateAsynDevice creates asynDevice if it doesn't already exist*/
static asynDevice *locateAsynDevice(asynPort *pasynPort, int addr)
{
    asynDevice *pasynDevice;

    assert(pasynPort);
    pasynDevice = (asynDevice *)ellFirst(&pasynPort->asynDeviceList);
    while(pasynDevice) {
        if(pasynDevice->addr == addr) return(pasynDevice);
        pasynDevice = (asynDevice *)ellNext(&pasynDevice->node);
    }
    if(!pasynDevice) {
        asynTracePvt *pasynTracePvt;
        pasynDevice = callocMustSucceed(1,sizeof(asynDevice),
            "asynManager:locateAsynDevice");
        pasynDevice->addr = addr;
        pasynTracePvt = &pasynDevice->trace;
        pasynTracePvt->traceBuffer = callocMustSucceed(
            DEFAULT_TRACE_TRUNCATE_SIZE,sizeof(char),
            "asynManager:locateAsynDevice");
        pasynTracePvt->traceMask = ASYN_TRACE_ERROR;
        pasynTracePvt->traceTruncateSize = DEFAULT_TRACE_TRUNCATE_SIZE;
        pasynTracePvt->traceBufferSize = DEFAULT_TRACE_TRUNCATE_SIZE;
        ellAdd(&pasynPort->asynDeviceList,&pasynDevice->node);
    }
    return(pasynDevice);
}

static void queueTimeoutCallback(void *pvt)
{
    asynUserPvt *pasynUserPvt = (asynUserPvt *)pvt;
    asynUser *pasynUser = &pasynUserPvt->user;
    int status;

    status = cancelRequest(pasynUser);
    if(status==0 && pasynUserPvt->timeoutCallback) {
        pasynUserPvt->timeoutCallback(pasynUser);
    }
}

static void portThread(asynPort *pasynPort)
{
    asynUserPvt *pasynUserPvt;
    asynUser *pasynUser;
    asynCommon *pasynCommon = 0;
    asynDevice *pasynDevice = 0;
    void *drvPvt = 0;
    int i;

    taskwdInsert(epicsThreadGetIdSelf(),0,0);
    /* find and call pasynCommon->connect */
    for(i=0; i<pasynPort->nasynInterface; i++) {
        asynInterface *pasynInterface = &pasynPort->paasynInterface[i];
        if(strcmp(pasynInterface->interfaceType,asynCommonType)==0) {
            pasynCommon = (asynCommon *)pasynInterface->pinterface;
            drvPvt = pasynInterface->drvPvt;
            break;
        }
    }
    if(pasynCommon) {
        asynStatus status;
        pasynPort->pasynUser->errorMessage[0] = '\0';
        status = pasynCommon->connect(drvPvt,pasynPort->pasynUser);
        if(status!=asynSuccess) {
            printf("asynManager:portThread could not connect %s\n",
                pasynPort->pasynUser->errorMessage);
        }
    }
    while(1) {
        if(epicsEventWait(pasynPort->notifyDeviceThread)!=epicsEventWaitOK) {
            errlogPrintf("asynManager::portThread epicsEventWait error");
        }
        while(1) {
            epicsMutexMustLock(pasynPort->lock);
            for(i=asynQueuePriorityHigh; i>=asynQueuePriorityLow; i--) {
                pasynUserPvt = (asynUserPvt *)ellFirst(&pasynPort->queueList[i]);
                while(pasynUserPvt){
		    pasynDevice = pasynUserPvt->pasynDevice;
		    assert(pasynDevice);
                    if(!pasynDevice->plockHolder
                    || pasynDevice->plockHolder==pasynUserPvt) {
                        ellDelete(&pasynPort->queueList[i],&pasynUserPvt->node);
                        pasynUserPvt->isQueued = FALSE;
                        break;
                    }
                    pasynUserPvt = (asynUserPvt *)ellNext(&pasynUserPvt->node);
                }
                if(pasynUserPvt) break;
            }
            if(!pasynUserPvt) {
                epicsMutexUnlock(pasynPort->lock);
                break;
            }
            pasynUser = asynUserPvtToAsynUser(pasynUserPvt);
	    assert(pasynDevice);
            if(pasynUserPvt->lockCount>0) {
                pasynDevice->plockHolder = pasynUserPvt;
            }
            if(pasynUserPvt->timer && pasynUserPvt->timeout>0.0) {
                epicsTimerCancel(pasynUserPvt->timer);
            }
            epicsMutexUnlock(pasynPort->lock);
            pasynUserPvt->queueCallback(pasynUser);
        }
    }
}

/* asynManager methods */
static void report(FILE *fd,int details)
{
    asynPort *pasynPort;

    if(!pasynBase) asynInit();
    pasynPort = (asynPort *)ellFirst(&pasynBase->asynPortList);
    while(pasynPort) {
	int nQueued, i;
        asynCommon *pasynCommon;
        void *asynCommonPvt = 0;
        asynDevice *pasynDevice;

        pasynCommon = 0;
	nQueued = 0;
	for(i=asynQueuePriorityLow; i<=asynQueuePriorityHigh; i++) {
	    nQueued += ellCount(&pasynPort->queueList[i]);
	}
	fprintf(fd,"%s thread %p nDevices %d nQueued %d\n",
            pasynPort->portName,pasynPort->threadid,
            ellCount(&pasynPort->asynDeviceList),nQueued);
	for(i=0; i<pasynPort->nasynInterface; i++) {
	    asynInterface *pasynInterface = &pasynPort->paasynInterface[i];
	    fprintf(fd,"    %s pinterface %p drvPvt %p\n",
                pasynInterface->interfaceType,
                pasynInterface->pinterface,
                pasynInterface->drvPvt);
            if(strcmp(pasynInterface->interfaceType,asynCommonType)==0) {
                pasynCommon = pasynInterface->pinterface;
                asynCommonPvt = pasynInterface->drvPvt;
            }
	}
        if(pasynCommon) {
            fprintf(fd,"    Calling asynCommon.report\n");
            pasynCommon->report(asynCommonPvt,fd,details);
        }
        pasynDevice = (asynDevice *)ellFirst(&pasynPort->asynDeviceList);
        while(pasynDevice) {
            fprintf(fd,"    addr %d\n",pasynDevice->addr);
	    if(pasynDevice->nprocessModules>0) {
	        fprintf(fd,"    %s is process module\n",
                    pasynDevice->processModuleName);
	    }
            pasynCommon = 0;
	    for(i=0; i<pasynDevice->nprocessModules; i++) {
	        asynInterface *pasynInterface = &pasynDevice->paprocessModule[i];
	        fprintf(fd,"        %s pinterface %p drvPvt %p\n",
                    pasynInterface->interfaceType,
                    pasynInterface->pinterface,
                    pasynInterface->drvPvt);
                if(strcmp(pasynInterface->interfaceType, asynCommonType)==0) {
                    pasynCommon = pasynInterface->pinterface;
                    asynCommonPvt = pasynInterface->drvPvt;
                }
	    }
            if(pasynCommon) {
                fprintf(fd,"    Calling asynCommon.report\n");
                pasynCommon->report(asynCommonPvt,fd,details);
            }
            pasynDevice = (asynDevice *)ellNext(&pasynDevice->node);
        }
        pasynPort = (asynPort *)ellNext(&pasynPort->node);
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
        pasynUserPvt->timeoutCallback = timeout;
        pasynUserPvt->timer = epicsTimerQueueCreateTimer(
            pasynBase->timerQueue,queueTimeoutCallback,pasynUserPvt);
    }
    return(pasynUser);
}

static asynStatus freeAsynUser(asynUser *pasynUser)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);

    if(pasynUserPvt->pasynPort) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:freeAsynUser asynUser is connected\n");
        return(asynError);
    }
    free(pasynUserPvt);
    return(asynSuccess);
}

static asynStatus connectDevice(asynUser *pasynUser,
    const char *portName, int addr)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynPort *pasynPort = locateAsynPort(portName);
    asynDevice *pasynDevice;

    assert(addr>=0);
    if(!pasynPort) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:connectPort %s not found\n",portName);
        return(asynError);
    }
    if(pasynUserPvt->pasynPort) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:connectPort already connected to device\n");
        return(asynError);
    }
    epicsMutexMustLock(pasynPort->lock);
    pasynUserPvt->pasynPort = pasynPort;
    pasynDevice = locateAsynDevice(pasynPort,addr);
    pasynUserPvt->pasynDevice = pasynDevice;
    epicsMutexUnlock(pasynPort->lock);
    return(asynSuccess);
}

static asynStatus disconnectDevice(asynUser *pasynUser)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynPort *pasynPort = pasynUserPvt->pasynPort;
    asynDevice *pasynDevice = pasynUserPvt->pasynDevice;

    if(!pasynBase) asynInit();
    if(!pasynPort || !pasynDevice) {
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
    epicsMutexMustLock(pasynPort->lock);
    pasynUserPvt->pasynPort = 0;
    pasynUserPvt->pasynDevice = 0;
    epicsMutexUnlock(pasynPort->lock);
    return(asynSuccess);
}

static asynInterface *findInterface(asynUser *pasynUser,
    const char *interfaceType,int processModuleOK)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynPort *pasynPort = pasynUserPvt->pasynPort;
    asynDevice *pasynDevice = pasynUserPvt->pasynDevice;
    asynInterface *pasynInterface;
    int i;

    if(!pasynPort) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:findInterface: not connected\n");
        return(0);
    }
    /*Look first for processModule then for asynInterface*/
    if(processModuleOK) for(i=0; i<pasynDevice->nprocessModules; i++) {
        pasynInterface = &pasynDevice->paprocessModule[i];
        if(strcmp(interfaceType,pasynInterface->interfaceType)==0) {
            return(pasynInterface);
        }
    }
    for(i=0; i<pasynPort->nasynInterface; i++) {
        pasynInterface = &pasynPort->paasynInterface[i];
        if(strcmp(interfaceType,pasynInterface->interfaceType)==0) {
            return(pasynInterface);
        }
    }
    return(0);
}

static asynInterface *findPortInterface(const char *portName,
    const char *interfaceType)
{
    asynPort *pasynPort = locateAsynPort(portName);
    asynInterface *pasynInterface;
    int i;

    if(!pasynPort) return(0);
    for(i=0; i<pasynPort->nasynInterface; i++) {
        pasynInterface = &pasynPort->paasynInterface[i];
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
    asynPort *pasynPort = pasynUserPvt->pasynPort;
    asynDevice *pasynDevice = pasynUserPvt->pasynDevice;

    assert(priority>=asynQueuePriorityLow && priority<=asynQueuePriorityHigh);
    if(!pasynPort || !pasynDevice) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::queueRequest not connected\n");
        return(asynError);
    }
    epicsMutexMustLock(pasynPort->lock);
    if(pasynUserPvt->isQueued) {
        epicsMutexUnlock(pasynPort->lock);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::queueRequest is already queued\n");
        return(asynError);
    }
    if(pasynDevice->plockHolder && pasynDevice->plockHolder==pasynUserPvt) {
        /*Add to front of list*/
        ellInsert(&pasynPort->queueList[priority],0,&pasynUserPvt->node);
    } else {
        /*Add to end of list*/
        ellAdd(&pasynPort->queueList[priority],&pasynUserPvt->node);
    }
    pasynUserPvt->isQueued = TRUE;
    pasynUserPvt->timeout = timeout;
    if(pasynUserPvt->timeout>0.0) {
        if(!pasynUserPvt->timeoutCallback) {
            printf("%s,%d queueRequest with timeout but no timeout callback\n",
                pasynPort->portName,pasynDevice->addr);
        } else {
            epicsTimerStartDelay(pasynUserPvt->timer,pasynUserPvt->timeout);
        }
    }
    epicsMutexUnlock(pasynPort->lock);
    epicsEventSignal(pasynPort->notifyDeviceThread);
    return(asynSuccess);
}

static int cancelRequest(asynUser *pasynUser)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynPort *pasynPort = pasynUserPvt->pasynPort;
    int i;

    epicsMutexMustLock(pasynPort->lock);
    for(i=asynQueuePriorityHigh; i>=asynQueuePriorityLow; i--) {
        pasynUserPvt = (asynUserPvt *)ellFirst(&pasynPort->queueList[i]);
	while(pasynUserPvt) {
	    if(pasynUser == &pasynUserPvt->user) {
	        ellDelete(&pasynPort->queueList[i],&pasynUserPvt->node);
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
    epicsMutexUnlock(pasynPort->lock);
    return(pasynUserPvt ? 0 : -1);
}

static asynStatus lock(asynUser *pasynUser)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynPort *pasynPort = pasynUserPvt->pasynPort;

    if(!pasynPort) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::lock not connected\n");
        return(asynError);
    }
    if(pasynUserPvt->isQueued) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::lock is queued\n");
        return(asynError);
    }
    epicsMutexMustLock(pasynPort->lock);
    pasynUserPvt->lockCount++;
    epicsMutexUnlock(pasynPort->lock);
    return(asynSuccess);
}

static asynStatus unlock(asynUser *pasynUser)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynPort *pasynPort = pasynUserPvt->pasynPort;
    asynDevice *pasynDevice = pasynUserPvt->pasynDevice;
    BOOL wasOwner = FALSE;

    if(!pasynPort || !pasynDevice) {
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
    epicsMutexMustLock(pasynPort->lock);
    pasynUserPvt->lockCount--;
    if(pasynDevice->plockHolder==pasynUserPvt) {
        pasynDevice->plockHolder = 0;
        wasOwner = TRUE;
    }
    epicsMutexUnlock(pasynPort->lock);
    if(wasOwner) epicsEventSignal(pasynPort->notifyDeviceThread);
    return(asynSuccess);
}

static int getAddr(asynUser *pasynUser)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);

    if(!pasynUserPvt->pasynDevice) return(-1);
    return(pasynUserPvt->pasynDevice->addr);
}

static asynStatus traceLock(asynUser *pasynUser)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynDevice *pasynDevice = pasynUserPvt->pasynDevice;

    if(!pasynDevice) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "asynManager::lock but not connected\n");
        return(asynError);
    }
    epicsMutexMustLock(pasynBase->lockTrace);
    return(asynSuccess);
}

static asynStatus traceUnlock(asynUser *pasynUser)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynDevice *pasynDevice = pasynUserPvt->pasynDevice;

    if(!pasynDevice) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "asynManager::unlock but not connected\n");
        return(asynError);
    }
    epicsMutexUnlock(pasynBase->lockTrace);
    return(asynSuccess);
}
static asynStatus setTraceMask(asynUser *pasynUser,int mask)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynDevice *pasynDevice = pasynUserPvt->pasynDevice;

    if(!pasynDevice) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::setTraceMask not connected\n");
        return(asynError);
    }
    pasynDevice->trace.traceMask = mask;
    return(asynSuccess);
}

static int getTraceMask(asynUser *pasynUser)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynDevice *pasynDevice = pasynUserPvt->pasynDevice;

    if(!pasynDevice) return(ASYN_TRACE_ERROR);
    return(pasynDevice->trace.traceMask);
}

static asynStatus setTraceIOMask(asynUser *pasynUser,int mask)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynDevice *pasynDevice = pasynUserPvt->pasynDevice;

    if(!pasynDevice) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::setTraceIOMask not connected\n");
        return(asynError);
    }
    pasynDevice->trace.traceIOMask = mask;
    return(asynSuccess);
}

static int getTraceIOMask(asynUser *pasynUser)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynDevice *pasynDevice = pasynUserPvt->pasynDevice;

    if(!pasynDevice) {
        printf("asynManager::getTraceIOMask but not connected\n");
        return(0);
    }
    return(pasynDevice->trace.traceIOMask);
}

static asynStatus setTraceFILE(asynUser *pasynUser,FILE *fd)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynDevice *pasynDevice = pasynUserPvt->pasynDevice;

    if(!pasynDevice) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::setTraceFILE not connected\n");
        return(asynError);
    }
    epicsMutexMustLock(pasynBase->lockTrace);
    pasynDevice->trace.fd = fd;
    epicsMutexUnlock(pasynBase->lockTrace);
    return(asynSuccess);
}

static FILE *getTraceFILE(asynUser *pasynUser)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynDevice *pasynDevice = pasynUserPvt->pasynDevice;

    if(!pasynDevice) {
        printf("asynManager::getTraceFILE but not connected\n");
        return(0);
    }
    return(pasynDevice->trace.fd);
}

static asynStatus setTraceIOTruncateSize(asynUser *pasynUser,int size)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynDevice *pasynDevice = pasynUserPvt->pasynDevice;
    asynTracePvt *pasynTracePvt;

    if(!pasynDevice) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::setTraceMask not connected\n");
        return(asynError);
    }
    epicsMutexMustLock(pasynBase->lockTrace);
    pasynTracePvt = &pasynDevice->trace;
    if(size>pasynTracePvt->traceBufferSize) {
        free(pasynTracePvt->traceBuffer);
        pasynTracePvt->traceBuffer = callocMustSucceed(size,sizeof(char),
            "asynTrace:setTraceIOTruncateSize");
        pasynTracePvt->traceBufferSize = size;
    }
    pasynTracePvt->traceTruncateSize = size;
    epicsMutexUnlock(pasynBase->lockTrace);
    return(asynSuccess);
}

static int getTraceIOTruncateSize(asynUser *pasynUser)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynDevice *pasynDevice = pasynUserPvt->pasynDevice;

    if(!pasynDevice) {
        printf("asynManager::getTraceFILE but not connected\n");
        return(0);
    }
    return pasynDevice->trace.traceTruncateSize;
}

static int tracePrint(asynUser *pasynUser,int reason, const char *pformat, ...)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynDevice *pasynDevice = pasynUserPvt->pasynDevice;
    asynTracePvt *pasynTracePvt;
    va_list pvar;
    int     nout = 0;
    FILE *fd;

    if(!pasynDevice) {
        va_start(pvar,pformat);
        nout = vfprintf(stdout,pformat,pvar);
        va_end(pvar);
        return(0);
    }
    pasynTracePvt = &pasynDevice->trace;
    if(!(reason&pasynTracePvt->traceMask)) return(0);
    epicsMutexMustLock(pasynBase->lockTrace);
    fd = (pasynTracePvt->fd) ? pasynTracePvt->fd : stdout;
    va_start(pvar,pformat);
    nout = vfprintf(fd,pformat,pvar);
    va_end(pvar);
    epicsMutexUnlock(pasynBase->lockTrace);
    return(nout);
}

static int tracePrintIO(asynUser *pasynUser,int reason,
    const char *buffer, int len,const char *pformat, ...)
{
    asynUserPvt *pasynUserPvt = asynUserToAsynUserPvt(pasynUser);
    asynDevice *pasynDevice = pasynUserPvt->pasynDevice;
    asynTracePvt *pasynTracePvt;
    va_list pvar;
    int     nout = 0;
    FILE *fd;
    int traceMask,traceIOMask,traceTruncateSize,nBytes;

    if(!pasynDevice) {
        printf("asynManager::printIO but not connected\n");
        return(0);
    }
    pasynTracePvt = &pasynDevice->trace;
    traceMask = pasynTracePvt->traceMask;
    traceIOMask = pasynTracePvt->traceIOMask;
    traceTruncateSize = pasynTracePvt->traceTruncateSize;
    if(!(reason&traceMask)) return(0);
    epicsMutexMustLock(pasynBase->lockTrace);
    fd = (pasynTracePvt->fd) ? pasynTracePvt->fd : stdout;
    va_start(pvar,pformat);
    nout += vfprintf(fd,pformat,pvar);
    va_end(pvar);
    nBytes = (len<traceTruncateSize) ? len : traceTruncateSize;
    if((traceIOMask&ASYN_TRACEIO_ASCII) && (nBytes>0)) {
        nout += fprintf(fd,"%.*s\n",nBytes,buffer);
    }
    if(traceIOMask&ASYN_TRACEIO_ESCAPE) {
        if(nBytes>0)
            nout += epicsStrPrintEscaped(fd,buffer,nBytes);
        nout += fprintf(fd,"\n");
    }
    if((traceIOMask&ASYN_TRACEIO_HEX) && (traceTruncateSize>0)) {
        int i;
        for(i=0; i<nBytes; i++) {
            if(i%20 == 0) nout += fprintf(fd,"\n");
            nout += fprintf(fd,"%2.2x ",(unsigned char)buffer[i]);
        }
        nout += fprintf(fd,"\n");
    }
    epicsMutexUnlock(pasynBase->lockTrace);
    return(nout);
}

static asynStatus registerPort(
    const char *portName,
    asynInterface *paasynInterface,int nasynInterface,
    unsigned int priority,unsigned int stackSize)
{
    asynPort *pasynPort;
    int      i;

    if(!pasynBase) asynInit();
    pasynPort = locateAsynPort(portName);
    if(pasynPort) {
        printf("asynCommon:registerDriver %s already registered\n",portName);
        return(asynError);
    }
    pasynPort = callocMustSucceed(1,sizeof(asynPort),"asynCommon:registerDriver");
    pasynPort->lock = epicsMutexMustCreate();
    pasynPort->pasynUser = createAsynUser(0,0);
    pasynPort->portName = portName;
    pasynPort->paasynInterface = paasynInterface;
    pasynPort->nasynInterface = nasynInterface;
    pasynPort->notifyDeviceThread = epicsEventMustCreate(epicsEventEmpty);
    pasynPort->priority = priority ? priority : epicsThreadPriorityMedium;
    pasynPort->stackSize = stackSize ?
                                stackSize :
                                epicsThreadGetStackSize(epicsThreadStackMedium);
    for(i=0; i<NUMBER_QUEUE_PRIORITIES; i++) ellInit(&pasynPort->queueList[i]);
    ellInit(&pasynPort->asynDeviceList);
    epicsMutexMustLock(pasynPort->lock);
    ellAdd(&pasynBase->asynPortList,&pasynPort->node);
    pasynPort->threadid = epicsThreadCreate(portName,pasynPort->priority,
                pasynPort->stackSize,(EPICSTHREADFUNC)portThread,pasynPort);
    if(!pasynPort->threadid){
        printf("asynCommon:registerDriver %s epicsThreadCreate failed \n",
            portName);
        return(asynError);
    }
    epicsMutexUnlock(pasynPort->lock);
    return(asynSuccess);
}

static asynStatus registerProcessModule(
    const char *processModuleName,const char *portName,int addr,
    asynInterface *paasynInterface,int nasynInterface)
{
    asynPort *pasynPort;
    asynDevice *pasynDevice;

    if(!pasynBase) asynInit();
    pasynPort = locateAsynPort(portName);
    if(!pasynPort) {
        printf("asynCommon:registerProcessModule %s not found\n",portName);
        return(asynError);
    }
    epicsMutexMustLock(pasynPort->lock);
    pasynDevice = locateAsynDevice(pasynPort,addr);
    if(pasynDevice->nprocessModules>0) {
        epicsMutexUnlock(pasynPort->lock);
        printf("asynCommon:registerProcessModule %s addr %d already "
		"has a process module registered\n",portName,addr);
        return(asynError);
    }
    pasynDevice->processModuleName = processModuleName;
    pasynDevice->paprocessModule = paasynInterface;
    pasynDevice->nprocessModules = nasynInterface;
    epicsMutexUnlock(pasynPort->lock);
    return(asynSuccess);
}

typedef struct setPortOptionArgs {
    const char *key;
    const char *val;
    asynCommon *pasynCommon;
    void *drvPvt;
    epicsEventId  done;
}setPortOptionArgs;

static void setPortOption(asynUser *pasynUser)
{
    setPortOptionArgs *poptionargs = (setPortOptionArgs *)pasynUser->userPvt;
    asynStatus status;

    status = poptionargs->pasynCommon->setPortOption(poptionargs->drvPvt,
        pasynUser,poptionargs->key,poptionargs->val);
    if(status!=asynSuccess) 
        printf("setPortOption failed %s\n",pasynUser->errorMessage);
    epicsEventSignal(poptionargs->done);
}

int asynSetPortOption(const char *portName, const char *key, const char *val)
{
    asynInterface *pasynInterface;
    setPortOptionArgs optionargs;
    asynUser *pasynUser;
    asynStatus status;

    if ((portName == NULL) || (key == NULL) || (val == NULL)) {
        printf("Missing argument\n");
        return asynError;
    }
    pasynUser = pasynManager->createAsynUser(setPortOption,0);
    pasynUser->userPvt = &optionargs;
    status = pasynManager->connectDevice(pasynUser,portName,0);
    if(status!=asynSuccess) {
        printf("connectDevice failed %s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return asynError;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,0);
    if(!pasynInterface) {
        printf("port %s not found\n",portName);
        return asynError;
    }
    optionargs.key = key;
    optionargs.val = val;
    optionargs.pasynCommon = (asynCommon *)pasynInterface->pinterface;
    optionargs. drvPvt = pasynInterface->drvPvt;
    optionargs.done = epicsEventMustCreate(epicsEventEmpty);
    status = pasynManager->queueRequest(pasynUser,0,0.0);
    epicsEventWait(optionargs.done);
    epicsEventDestroy(optionargs.done);
    pasynManager->freeAsynUser(pasynUser);
    return(0);
}

typedef struct showPortOptionArgs {
    const char *key;
    asynCommon *pasynCommon;
    void *drvPvt;
    epicsEventId  done;
}showPortOptionArgs;

static void showPortOption(asynUser *pasynUser)
{
    showPortOptionArgs *poptionargs = (showPortOptionArgs *)pasynUser->userPvt;
    asynStatus status;
    char val[100];

    pasynUser->errorMessage[0] = '\0';
    status = poptionargs->pasynCommon->getPortOption(poptionargs->drvPvt,
        pasynUser,poptionargs->key,val,sizeof(val));
    if(status!=asynSuccess) {
        printf("getPortOption failed %s\n",pasynUser->errorMessage);
    } else {
        printf("%s=%s\n",poptionargs->key,val);
    }
    epicsEventSignal(poptionargs->done);
}

int asynShowPortOption(const char *portName, const char *key)
{
    asynInterface *pasynInterface;
    showPortOptionArgs optionargs;
    asynUser *pasynUser;
    asynStatus status;

    if ((portName == NULL) || (key == NULL) ) {
        printf("Missing argument\n");
        return asynError;
    }
    pasynUser = pasynManager->createAsynUser(showPortOption,0);
    pasynUser->userPvt = &optionargs;
    status = pasynManager->connectDevice(pasynUser,portName,0);
    if(status!=asynSuccess) {
        printf("connectDevice failed %s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return 1;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,0);
    if(!pasynInterface) {
        printf("port %s not found\n",portName);
        return asynError;
    }
    optionargs.key = key;
    optionargs.pasynCommon = (asynCommon *)pasynInterface->pinterface;
    optionargs. drvPvt = pasynInterface->drvPvt;
    optionargs.done = epicsEventMustCreate(epicsEventEmpty);
    status = pasynManager->queueRequest(pasynUser,0,0.0);
    epicsEventWait(optionargs.done);
    epicsEventDestroy(optionargs.done);
    pasynManager->freeAsynUser(pasynUser);
    return(0);
}

static const iocshArg asynReportArg0 = {"filename", iocshArgString};
static const iocshArg asynReportArg1 = {"level", iocshArgInt};
static const iocshArg *const asynReportArgs[] = {&asynReportArg0,&asynReportArg1};
static const iocshFuncDef asynReportDef = {"asynReport", 2, asynReportArgs};
/*asynReport is an extern just for vxWorks*/
int asynReport(const char *filename, int level)
{
    FILE *fp;

    if(!filename || filename[0]==0) {
        fp = stdout;
    } else {
        fp = fopen(filename,"w+");
        if(!fp) {
            printf("fopen failed %s\n",strerror(errno));
            return(-1);
        }
    }
    report(fp,level);
    return(0);
}
static void asynReportCall(const iocshArgBuf * args) {
    asynReport(args[0].sval,args[1].ival);
}

static const iocshArg asynSetPortOptionArg0 = {"portName", iocshArgString};
static const iocshArg asynSetPortOptionArg1 = {"key", iocshArgString};
static const iocshArg asynSetPortOptionArg2 = {"value", iocshArgString};
static const iocshArg *const asynSetPortOptionArgs[] = {
              &asynSetPortOptionArg0, &asynSetPortOptionArg1,
              &asynSetPortOptionArg2};
static const iocshFuncDef asynSetPortOptionDef = {"asynSetPortOption", 3, asynSetPortOptionArgs};
static void asynSetPortOptionCall(const iocshArgBuf * args) {
    asynSetPortOption(args[0].sval,args[1].sval,args[2].sval);
}

static const iocshArg asynShowPortOptionArg0 = {"portName", iocshArgString};
static const iocshArg asynShowPortOptionArg1 = {"key", iocshArgString};
static const iocshArg *const asynShowPortOptionArgs[] = {
              &asynShowPortOptionArg0, &asynShowPortOptionArg1};
static const iocshFuncDef asynShowPortOptionDef = {"asynShowPortOption", 2, asynShowPortOptionArgs};
static void asynShowPortOptionCall(const iocshArgBuf * args) {
    asynShowPortOption(args[0].sval,args[1].sval);
}

static const iocshArg asynSetTraceMaskArg0 = {"portName", iocshArgString};
static const iocshArg asynSetTraceMaskArg1 = {"addr", iocshArgInt};
static const iocshArg asynSetTraceMaskArg2 = {"mask", iocshArgInt};
static const iocshArg *const asynSetTraceMaskArgs[] = {
    &asynSetTraceMaskArg0,&asynSetTraceMaskArg1,&asynSetTraceMaskArg2};
static const iocshFuncDef asynSetTraceMaskDef =
    {"asynSetTraceMask", 3, asynSetTraceMaskArgs};
/*asynSetTraceMask is an extern just for vxWorks*/
int asynSetTraceMask(const char *portName,int addr,int mask)
{
    asynUser *pasynUser;
    asynStatus status;

    pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return(-1);
    }
    status = pasynTrace->setTraceMask(pasynUser,mask);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
    }
    pasynManager->freeAsynUser(pasynUser);
    return(0);
}
static void asynSetTraceMaskCall(const iocshArgBuf * args) {
    const char *portName = args[0].sval;
    int addr = args[1].ival;
    int mask = args[2].ival;
    asynSetTraceMask(portName,addr,mask);
}

static const iocshArg asynSetTraceIOMaskArg0 = {"portName", iocshArgString};
static const iocshArg asynSetTraceIOMaskArg1 = {"addr", iocshArgInt};
static const iocshArg asynSetTraceIOMaskArg2 = {"mask", iocshArgInt};
static const iocshArg *const asynSetTraceIOMaskArgs[] = {
    &asynSetTraceIOMaskArg0,&asynSetTraceIOMaskArg1,&asynSetTraceIOMaskArg2};
static const iocshFuncDef asynSetTraceIOMaskDef =
    {"asynSetTraceIOMask", 3, asynSetTraceIOMaskArgs};
/*asynSetTraceIOMask is an extern just for vxWorks*/
int asynSetTraceIOMask(const char *portName,int addr,int mask)
{
    asynUser *pasynUser;
    asynStatus status;

    pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return(-1);
    }
    status = pasynTrace->setTraceIOMask(pasynUser,mask);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
    }
    pasynManager->freeAsynUser(pasynUser);
    return(0);
}
static void asynSetTraceIOMaskCall(const iocshArgBuf * args) {
    const char *portName = args[0].sval;
    int addr = args[1].ival;
    int mask = args[2].ival;
    asynSetTraceIOMask(portName,addr,mask);
}

static void asyn(void)
{
    static int firstTime = 1;
    if(!firstTime) return;
    firstTime = 0;
    iocshRegister(&asynReportDef,asynReportCall);
    iocshRegister(&asynSetPortOptionDef,asynSetPortOptionCall);
    iocshRegister(&asynShowPortOptionDef,asynShowPortOptionCall);
    iocshRegister(&asynSetTraceMaskDef,asynSetTraceMaskCall);
    iocshRegister(&asynSetTraceIOMaskDef,asynSetTraceIOMaskCall);
}
epicsExportRegistrar(asyn);
