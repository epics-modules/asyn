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
#include "asynDriver.h"

#define BOOL int
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#define ERROR_MESSAGE_SIZE 160
#define NUMBER_QUEUE_PRIORITIES (asynQueuePriorityConnect + 1)
#define DEFAULT_TRACE_TRUNCATE_SIZE 80
#define DEFAULT_TRACE_BUFFER_SIZE 80
#define DEFAULT_SECONDS_BETWEEN_PORT_CONNECT 20
#define DEFAULT_AUTOCONNECT_TIMEOUT 0.5
#define DEFAULT_QUEUE_LOCK_PORT_TIMEOUT 2.0

/* This is taken from dbDefs.h, which we don't want to include */
/* Subtract member byte offset, returning pointer to parent object */
#ifndef CONTAINER
# ifdef __GNUC__
#   define CONTAINER(ptr, structure, member) ({                     \
        const __typeof(((structure*)0)->member) *_ptr = (ptr);      \
        (structure*)((char*)_ptr - offsetof(structure, member));    \
    })
# else
#   define CONTAINER(ptr, structure, member) \
        ((structure*)((char*)(ptr) - offsetof(structure, member)))
# endif
#endif

typedef struct tracePvt tracePvt;
typedef struct userPvt userPvt;
typedef struct port port;
typedef struct device device;

typedef enum {
    traceFileErrlog,traceFileStdout,traceFileStderr,traceFileFP
}traceFileType;

struct tracePvt {
    int           traceMask;
    int           traceIOMask;
    int           traceInfoMask;
    traceFileType type;
    FILE          *fp;
    size_t        traceTruncateSize;
    size_t        traceBufferSize;
    char          *traceBuffer;
};

#define nMemList 9
static size_t memListSize[nMemList] =
    {16,32,64,128,256,512,1024,2048,4096};
typedef struct memNode {
    ELLNODE node;
    void    *memory;
}memNode;

/*
 * Ensure adequate alignment
 */
#define NODESIZE (((sizeof(memNode)+15)/16)*16)

typedef struct asynBase {
    ELLLIST           asynPortList;
    ELLLIST           asynUserFreeList;
    ELLLIST           interruptNodeFree;
    epicsTimerQueueId timerQueue;
    epicsMutexId      lock;
    epicsMutexId      lockTrace;
    tracePvt          trace;
    ELLLIST           memList[nMemList];
    /* following for connectPort */
    epicsTimerQueueId connectPortTimerQueue;
    double            autoConnectTimeout;
}asynBase;
static asynBase *pasynBase = 0;

typedef struct interruptBase {
    ELLLIST      callbackList;
    ELLLIST      addRemoveList;
    BOOL         callbackActive;
    BOOL         listModified;
    port         *pport;
    asynInterface *pasynInterface;
}interruptBase;

typedef struct interruptNodePvt {
    ELLNODE  addRemoveNode;
    BOOL     isOnList;
    BOOL     isOnAddRemoveList;
    epicsEventId  callbackDone;
    interruptBase *pinterruptBase;
    interruptNode nodePublic;
}interruptNodePvt;

typedef struct interfaceNode {
    ELLNODE       node;
    asynInterface *pasynInterface;
    interruptBase *pinterruptBase;
}interfaceNode;

typedef struct dpCommon { /*device/port common fields*/
    BOOL           enabled;
    BOOL           connected;
    BOOL           autoConnect;
    BOOL           autoConnectActive;
    userPvt        *pblockProcessHolder;
    ELLLIST        interposeInterfaceList;
    ELLLIST        exceptionUserList;
    ELLLIST        exceptionNotifyList;
    BOOL           exceptionActive;
    epicsTimeStamp lastConnectDisconnect;
    unsigned long  numberConnects;
    tracePvt       trace;
    port           *pport;
    device         *pdevice; /* 0 if port.dpc*/
}dpCommon;

typedef struct exceptionUser {
    ELLNODE           node;
    ELLNODE           notifyNode;
    exceptionCallback callback;
    asynUser          *pasynUser;
    epicsEventId      notify;
}exceptionUser;

typedef enum {callbackIdle,callbackActive,callbackCanceled}callbackState;
struct userPvt {
    ELLNODE       node;        /*For asynPort.queueList*/
    /* timer,...,state are for queueRequest callbacks*/
    epicsTimerId  timer;
    epicsEventId  callbackDone;
    userCallback  processUser;
    userCallback  timeoutUser;
    double        timeout;
    callbackState state;
    unsigned int  blockPortCount;
    unsigned int  blockDeviceCount;
    BOOL          isBlockHolder;
    port          *pport;
    device        *pdevice;
    exceptionUser *pexceptionUser;
    BOOL          freeAfterCallback;
    BOOL          isQueued;
    asynUser      user;
};

struct device {
    ELLNODE   node;     /*For asynPort.deviceList*/
    dpCommon  dpc;
    int       addr;
};

typedef enum portConnectStatus {
    portConnectSuccess,
    portConnectDevice,
    portConnectFindInterface,
    portConnectDriver
} portConnectStatus;

struct port {
    ELLNODE       node;  /*For asynBase.asynPortList*/
    char          *portName;
    epicsMutexId  asynManagerLock; /*for asynManager*/
    epicsMutexId  synchronousLock; /*for synchronous drivers*/
    dpCommon      dpc;
    ELLLIST       deviceList;
    ELLLIST       interfaceList;
    int           attributes;
    /* The following are for autoConnect*/
    asynUser      *pasynUser;
    double        secondsBetweenPortConnect;
    portConnectStatus previousConnectStatus;
    /* The following are for asynLockPortNotify */
    asynLockPortNotify *pasynLockPortNotify;
    void          *lockPortNotifyPvt;
    /*The following are only initialized/used if attributes&ASYN_CANBLOCK*/
    ELLLIST       queueList[NUMBER_QUEUE_PRIORITIES];
    BOOL          queueStateChange;
    epicsEventId  notifyPortThread;
    epicsThreadId threadid;
    userPvt       *pblockProcessHolder;
    /* following are for portConnect */
    asynUser      *pconnectUser;
    asynInterface *pcommonInterface;
    epicsTimerId  connectTimer;
    epicsThreadPrivateId queueLockPortId;
    double        queueLockPortTimeout;
    /* The following are for timestamp support */
    epicsTimeStamp timeStamp;
    timeStampCallback timeStampSource;
    void          *timeStampPvt;
};

typedef struct queueLockPortPvt {
    epicsEventId  queueLockPortEvent;
    epicsMutexId  queueLockPortMutex;
    unsigned int  queueLockPortCount;
}queueLockPortPvt;

#define interruptNodeToPvt(pinterruptNode) \
    ((interruptNodePvt *) ((char *)(pinterruptNode) \
           - ( (char *)&(((interruptNodePvt *)0)->nodePublic) - (char *)0 ) ) )
#define userPvtToAsynUser(p) (&p->user)
#define asynUserToUserPvt(p) \
  ((userPvt *) ((char *)(p) \
          - ( (char *)&(((userPvt *)0)->user) - (char *)0 ) ) )
#define  notifyNodeToExceptionUser(p) \
  ((exceptionUser *) ((char *)(p) \
          - ( (char *)&(((exceptionUser *)0)->notifyNode) - (char *)0 ) ) )

/* internal methods */
static void tracePvtInit(tracePvt *ptracePvt);
static void tracePvtFree(tracePvt *ptracePvt);
static void asynInit(void);
static void dpCommonInit(port *pport,device *pdevice,BOOL autoConnect);
static void dpCommonFree(dpCommon *pdpCommon);
static dpCommon *findDpCommon(userPvt *puserPvt);
static tracePvt *findTracePvt(userPvt *puserPvt);
static port *locatePort(const char *portName);
static device *locateDevice(port *pport,int addr,BOOL allocNew);
static interfaceNode *locateInterfaceNode(
            ELLLIST *plist,const char *interfaceType,BOOL allocNew);
static void exceptionOccurred(asynUser *pasynUser,asynException exception);
static void queueTimeoutCallback(void *pvt);
/*autoConnectDevice must be called with asynManagerLock held*/
static BOOL autoConnectDevice(port *pport,device *pdevice);
static void connectAttempt(dpCommon *pdpCommon);
static void portThread(port *pport);
/* functions for portConnect */
static void initPortConnect(port *ppport);
static void portConnectTimerCallback(void *pvt);
static void portConnectProcessCallback(asynUser *pasynUser);
    
/* asynManager methods */
static void report(FILE *fp,int details,const char*portName);
static asynUser *createAsynUser(userCallback process, userCallback timeout);
static asynUser *duplicateAsynUser(asynUser *pasynUser,
   userCallback queue, userCallback timeout);
static asynStatus freeAsynUser(asynUser *pasynUser);
static void       *memMalloc(size_t size);
static void       memFree(void *pmem,size_t size);
static asynStatus isMultiDevice(asynUser *pasynUser,
    const char *portName,int *yesNo);
static asynStatus connectDevice(asynUser *pasynUser,
    const char *portName,int addr);
static asynStatus disconnect(asynUser *pasynUser);
static asynStatus exceptionCallbackAdd(asynUser *pasynUser,
    exceptionCallback callback);
static asynStatus exceptionCallbackRemove(asynUser *pasynUser);
static asynInterface *findInterface(asynUser *pasynUser,
    const char *interfaceType,int interposeInterfaceOK);
static asynStatus queueRequest(asynUser *pasynUser,
    asynQueuePriority priority,double timeout);
static asynStatus cancelRequest(asynUser *pasynUser,int *wasQueued);
static asynStatus blockProcessCallback(asynUser *pasynUser, int allDevices);
static asynStatus unblockProcessCallback(asynUser *pasynUser, int allDevices);
static asynStatus lockPort(asynUser *pasynUser);
static asynStatus unlockPort(asynUser *pasynUser);
static asynStatus queueLockPort(asynUser *pasynUser);
static asynStatus queueUnlockPort(asynUser *pasynUser);
static asynStatus setQueueLockPortTimeout(asynUser *pasynUser, double timeout);
static asynStatus canBlock(asynUser *pasynUser,int *yesNo);
static asynStatus getAddr(asynUser *pasynUser,int *addr);
static asynStatus getPortName(asynUser *pasynUser,const char **pportName);
static asynStatus registerPort(const char *portName,
    int attributes,int autoConnect,
    unsigned int priority,unsigned int stackSize);
static asynStatus registerInterface(const char *portName,
    asynInterface *pasynInterface);
static asynStatus exceptionConnect(asynUser *pasynUser);
static asynStatus exceptionDisconnect(asynUser *pasynUser);
static asynStatus interposeInterface(const char *portName, int addr,
    asynInterface *pasynInterface,asynInterface **ppPrev);
static asynStatus enable(asynUser *pasynUser,int yesNo);
static asynStatus autoConnectAsyn(asynUser *pasynUser,int yesNo);
static asynStatus isConnected(asynUser *pasynUser,int *yesNo);
static asynStatus isEnabled(asynUser *pasynUser,int *yesNo);
static asynStatus isAutoConnect(asynUser *pasynUser,int *yesNo);
static asynStatus setAutoConnectTimeout(double timeout);
static asynStatus waitConnect(asynUser *pasynUser, double timeout);
static asynStatus registerInterruptSource(const char *portName,
    asynInterface *pasynInterface, void **pasynPvt);
static asynStatus getInterruptPvt(asynUser *pasynUser,
    const char *interfaceType, void **pasynPvt);
static interruptNode *createInterruptNode(void *pasynPvt);
static asynStatus freeInterruptNode(asynUser *pasynUser,interruptNode *pnode);
static asynStatus addInterruptUser(asynUser *pasynUser,
                                   interruptNode*pinterruptNode);
static asynStatus removeInterruptUser(asynUser *pasynUser,
                                   interruptNode*pinterruptNode);
static asynStatus interruptStart(void *pasynPvt,ELLLIST **plist);
static asynStatus interruptEnd(void *pasynPvt);
static void defaultTimeStampSource(void *userPvt, epicsTimeStamp *pTimeStamp);
static asynStatus registerTimeStampSource(asynUser *pasynUser, void *userPvt, timeStampCallback callback);
static asynStatus unregisterTimeStampSource(asynUser *pasynUser);
static asynStatus updateTimeStamp(asynUser *pasynUser);
static asynStatus getTimeStamp(asynUser *pasynUser, epicsTimeStamp *pTimeStamp);
static asynStatus setTimeStamp(asynUser *pasynUser, const epicsTimeStamp *pTimeStamp);
static const char *strStatus(asynStatus status);

static asynManager manager = {
    report,
    createAsynUser,
    duplicateAsynUser,
    freeAsynUser,
    memMalloc,
    memFree,
    isMultiDevice,
    connectDevice,
    disconnect,
    exceptionCallbackAdd,
    exceptionCallbackRemove,
    findInterface,
    queueRequest,
    cancelRequest,
    blockProcessCallback,
    unblockProcessCallback,
    lockPort,
    unlockPort,
    queueLockPort,
    queueUnlockPort,
    setQueueLockPortTimeout,
    canBlock,
    getAddr,
    getPortName,
    registerPort,
    registerInterface,
    exceptionConnect,
    exceptionDisconnect,
    interposeInterface,
    enable,
    autoConnectAsyn,
    isConnected,
    isEnabled,
    isAutoConnect,
    setAutoConnectTimeout,
    waitConnect,
    registerInterruptSource,
    getInterruptPvt,
    createInterruptNode,
    freeInterruptNode,
    addInterruptUser,
    removeInterruptUser,
    interruptStart,
    interruptEnd,
    registerTimeStampSource,
    unregisterTimeStampSource,
    updateTimeStamp,
    getTimeStamp,
    setTimeStamp,
    strStatus
};
epicsShareDef asynManager *pasynManager = &manager;

/* asynTrace methods */
static asynStatus traceLock(asynUser *pasynUser);
static asynStatus traceUnlock(asynUser *pasynUser);
static asynStatus setTraceMask(asynUser *pasynUser,int mask);
static int        getTraceMask(asynUser *pasynUser);
static asynStatus setTraceIOMask(asynUser *pasynUser,int mask);
static int        getTraceIOMask(asynUser *pasynUser);
static asynStatus setTraceInfoMask(asynUser *pasynUser,int mask);
static int        getTraceInfoMask(asynUser *pasynUser);
static asynStatus setTraceFile(asynUser *pasynUser,FILE *fp);
static FILE       *getTraceFile(asynUser *pasynUser);
static asynStatus setTraceIOTruncateSize(asynUser *pasynUser,size_t size);
static size_t     getTraceIOTruncateSize(asynUser *pasynUser);
static int        tracePrint(asynUser *pasynUser,
                      int reason, const char *pformat, ...);
static int        tracePrintSource(asynUser *pasynUser,
                      int reason, const char *file, int line, const char *pformat, ...);
static int        traceVprint(asynUser *pasynUser,
                      int reason, const char *pformat, va_list pvar);
static int        traceVprintSource(asynUser *pasynUser,
                      int reason, const char *file, int line, const char *pformat, va_list pvar);
static int        tracePrintIO(asynUser *pasynUser,int reason,
                      const char *buffer, size_t len,const char *pformat, ...);
static int        tracePrintIOSource(asynUser *pasynUser,int reason,
                      const char *buffer, size_t len, const char *file, int line, const char *pformat, ...);
static int        traceVprintIO(asynUser *pasynUser,int reason,
                      const char *buffer, size_t len,const char *pformat, va_list pvar);
static int        traceVprintIOSource(asynUser *pasynUser,int reason,
                      const char *buffer, size_t len, const char *file, int line, const char *pformat, va_list pvar);
static asynTrace asynTraceManager = {
    traceLock,
    traceUnlock,
    setTraceMask,
    getTraceMask,
    setTraceIOMask,
    getTraceIOMask,
    setTraceInfoMask,
    getTraceInfoMask,
    setTraceFile,
    getTraceFile,
    setTraceIOTruncateSize,
    getTraceIOTruncateSize,
    tracePrint,
    tracePrintSource,
    traceVprint,
    traceVprintSource,
    tracePrintIO,
    tracePrintIOSource,
    traceVprintIO,
    traceVprintIOSource
};
epicsShareDef asynTrace *pasynTrace = &asynTraceManager;

/*internal methods */
static void tracePvtInit(tracePvt *ptracePvt)
{
    ptracePvt->traceBuffer = callocMustSucceed(
        DEFAULT_TRACE_BUFFER_SIZE,sizeof(char),
        "asynManager:tracePvtInit");
    ptracePvt->traceMask = ASYN_TRACE_ERROR;
    ptracePvt->traceInfoMask = ASYN_TRACEINFO_TIME;
    ptracePvt->traceTruncateSize = DEFAULT_TRACE_TRUNCATE_SIZE;
    ptracePvt->traceBufferSize = DEFAULT_TRACE_BUFFER_SIZE;
    ptracePvt->type = traceFileStderr;
}

static void tracePvtFree(tracePvt *ptracePvt)
{
    assert(ptracePvt->fp==0);
    free(ptracePvt->traceBuffer);
}

static void reportConnectStatus(port *pport, portConnectStatus status, const char *fmt, ...)
{
    va_list args;

    if (pport->previousConnectStatus != status) {
        pport->previousConnectStatus = status;
        va_start(args, fmt);
        pasynTrace->vprint(pport->pasynUser, ASYN_TRACE_ERROR, fmt, args);
        va_end(args);
    }
}

static void asynInit(void)
{
    int i;

    if(pasynBase) return;
    pasynBase = callocMustSucceed(1,sizeof(asynBase),"asynInit");
    ellInit(&pasynBase->asynPortList);
    ellInit(&pasynBase->asynUserFreeList);
    ellInit(&pasynBase->interruptNodeFree);
    pasynBase->timerQueue = epicsTimerQueueAllocate(
        1,epicsThreadPriorityScanLow);
    pasynBase->lock = epicsMutexMustCreate();
    pasynBase->lockTrace = epicsMutexMustCreate();
    tracePvtInit(&pasynBase->trace);
    for(i=0; i<nMemList; i++) ellInit(&pasynBase->memList[i]);
    pasynBase->connectPortTimerQueue = epicsTimerQueueAllocate(
        0,epicsThreadPriorityScanLow);
    pasynBase->autoConnectTimeout = DEFAULT_AUTOCONNECT_TIMEOUT;
}

static void dpCommonInit(port *pport,device *pdevice,BOOL autoConnect)
{
    dpCommon *pdpCommon;

    if(pdevice) {
        pdpCommon = &pdevice->dpc;
    } else {
        pdpCommon = &pport->dpc;
    }
    pdpCommon->enabled = TRUE;
    pdpCommon->connected = FALSE;
    pdpCommon->autoConnect = autoConnect;
    ellInit(&pdpCommon->interposeInterfaceList);
    ellInit(&pdpCommon->exceptionUserList);
    ellInit(&pdpCommon->exceptionNotifyList);
    pdpCommon->pport = pport;
    pdpCommon->pdevice = pdevice;
    tracePvtInit(&pdpCommon->trace);
}

static void dpCommonFree(dpCommon *pdpCommon)
{
    tracePvtFree(&pdpCommon->trace);
}

static dpCommon *findDpCommon(userPvt *puserPvt)
{
    port *pport = puserPvt->pport;
    device *pdevice = puserPvt->pdevice;

    if(!pport) return(0);
    if(!(pport->attributes&ASYN_MULTIDEVICE) || !pdevice) return(&pport->dpc);
    return(&pdevice->dpc);
}

static tracePvt *findTracePvt(userPvt *puserPvt)
{
    dpCommon *pdpCommon = findDpCommon(puserPvt);
    if(pdpCommon) return(&pdpCommon->trace);
    return(&pasynBase->trace);
}

/*locatePort returns 0 if portName is not registered*/
static port *locatePort(const char *portName)
{
    port *pport;

    if(!pasynBase) asynInit();
    epicsMutexMustLock(pasynBase->lock);
    pport = (port *)ellFirst(&pasynBase->asynPortList);
    while(pport) {
        if(strcmp(pport->portName,portName)==0) break;
        pport = (port *)ellNext(&pport->node);
    }
    epicsMutexUnlock(pasynBase->lock);
    return pport;
}

static device *locateDevice(port *pport,int addr,BOOL allocNew)
{
    device *pdevice;

    assert(pport);
    if(!(pport->attributes&ASYN_MULTIDEVICE) || addr < 0) return(0);
    pdevice = (device *)ellFirst(&pport->deviceList);
    while(pdevice) {
        if(pdevice->addr == addr) return pdevice;
        pdevice = (device *)ellNext(&pdevice->node);
    }
    if(!pdevice && allocNew) {
        pdevice = callocMustSucceed(1,sizeof(device),
            "asynManager:locateDevice");
        pdevice->addr = addr;
        dpCommonInit(pport,pdevice,pport->dpc.autoConnect);
        ellAdd(&pport->deviceList,&pdevice->node);
    }
    return pdevice;
}

static interfaceNode *locateInterfaceNode(
            ELLLIST *plist,const char *interfaceType,BOOL allocNew)
{
    interfaceNode *pinterfaceNode;

    pinterfaceNode = (interfaceNode *)ellFirst(plist);
    while(pinterfaceNode) {
        asynInterface *pif = pinterfaceNode->pasynInterface;
        if(strcmp(pif->interfaceType,interfaceType)==0) break;
        pinterfaceNode = (interfaceNode *)ellNext(&pinterfaceNode->node);
    }
    if(!pinterfaceNode && allocNew) {
        pinterfaceNode = callocMustSucceed(1,sizeof(interfaceNode),
            "asynManager::locateInterfaceNode");
        ellAdd(plist,&pinterfaceNode->node);
    }
    return pinterfaceNode;
}

/* While an exceptionActive exceptionCallbackAdd and exceptionCallbackRemove
   will wait to be notified that exceptionActive is no longer true.  */
static void announceExceptionOccurred(port *pport, device *pdevice, asynException exception)
{
    dpCommon *pdpCommon = (pdevice ? &pdevice->dpc : &pport->dpc);
    exceptionUser *pexceptionUser;
    ELLNODE *pn;

    assert(pport&&pdpCommon);
    epicsMutexMustLock(pport->asynManagerLock);
    pdpCommon->exceptionActive = TRUE;
    epicsMutexUnlock(pport->asynManagerLock);
    pexceptionUser = (exceptionUser *)ellFirst(&pdpCommon->exceptionUserList);
    while(pexceptionUser) {
        pexceptionUser->callback(pexceptionUser->pasynUser,exception);
        pexceptionUser = (exceptionUser *)ellNext(&pexceptionUser->node);
    }
    epicsMutexMustLock(pport->asynManagerLock);
    while((pn = ellFirst(&pdpCommon->exceptionNotifyList))) {
        pexceptionUser = CONTAINER(pn, exceptionUser, notifyNode);
        epicsEventSignal(pexceptionUser->notify);
        ellDelete(&pdpCommon->exceptionNotifyList,&pexceptionUser->notifyNode);
    }
    pdpCommon->exceptionActive = FALSE;
    pport->queueStateChange = TRUE;
    epicsMutexUnlock(pport->asynManagerLock);
    if(pport->attributes&ASYN_CANBLOCK)
        epicsEventSignal(pport->notifyPortThread);
}
static void exceptionOccurred(asynUser *pasynUser,asynException exception)
{
    userPvt    *puserPvt = asynUserToUserPvt(pasynUser);
    port       *pport = puserPvt->pport;
    device     *pdevice = puserPvt->pdevice;

    announceExceptionOccurred(pport, pdevice, exception);
}

static void queueTimeoutCallback(void *pvt)
{
    userPvt  *puserPvt = (userPvt *)pvt;
    asynUser *pasynUser = &puserPvt->user;
    port     *pport = puserPvt->pport;
    int      i;

    epicsMutexMustLock(pport->asynManagerLock);
    if(!puserPvt->isQueued) {
        epicsMutexUnlock(pport->asynManagerLock);
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s asynManager:queueTimeoutCallback but not queued\n",
            pport->portName );
        return;
    }
    for(i=asynQueuePriorityConnect; i>=asynQueuePriorityLow; i--) {
        puserPvt = (userPvt *)ellFirst(&pport->queueList[i]);
        while(puserPvt) {
            if(pasynUser == &puserPvt->user) {
                ellDelete(&pport->queueList[i],&puserPvt->node);
                break;
            }
            puserPvt = (userPvt *)ellNext(&puserPvt->node);
        }
        if (puserPvt) break;
    }
    if(!puserPvt) {
        epicsMutexUnlock(pport->asynManagerLock);
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s asynManager:queueTimeoutCallback LOGIC ERROR\n",
            pport->portName);
        return;
    }
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s asynManager:queueTimeoutCallback\n", pport->portName);
    puserPvt->isQueued = FALSE;
    pport->queueStateChange = TRUE;
    if(puserPvt->timeoutUser) {
        puserPvt->state = callbackActive;
        epicsMutexUnlock(pport->asynManagerLock);
        puserPvt->timeoutUser(pasynUser);
        epicsMutexMustLock(pport->asynManagerLock);
        if(puserPvt->state==callbackCanceled)
            epicsEventSignal(puserPvt->callbackDone);
        puserPvt->state = callbackIdle;
        if(puserPvt->freeAfterCallback) {
            puserPvt->freeAfterCallback = FALSE;
            epicsMutexMustLock(pasynBase->lock);
            ellAdd(&pasynBase->asynUserFreeList,&puserPvt->node);
            epicsMutexUnlock(pasynBase->lock);
        }
    }
    epicsMutexUnlock(pport->asynManagerLock);
    epicsEventSignal(pport->notifyPortThread);
}

/*autoConnectDevice must be called with asynManagerLock held*/
static BOOL autoConnectDevice(port *pport,device *pdevice)
{
    if(!pport->dpc.connected
    &&  pport->dpc.autoConnect
    && !pport->dpc.autoConnectActive) {
        epicsTimeStamp now;

        epicsTimeGetCurrent(&now);
        if(epicsTimeDiffInSeconds(
             &now,&pport->dpc.lastConnectDisconnect) < 2.0) return FALSE;
        epicsTimeGetCurrent(&pport->dpc.lastConnectDisconnect);
        pport->dpc.autoConnectActive = TRUE;
        epicsMutexUnlock(pport->asynManagerLock);
        connectAttempt(&pport->dpc);
        epicsMutexMustLock(pport->asynManagerLock);
        pport->dpc.autoConnectActive = FALSE;
    }
    if(!pport->dpc.connected) return FALSE;
    if(!pdevice) return TRUE;
    if(!pdevice->dpc.connected
    &&  pdevice->dpc.autoConnect
    && !pdevice->dpc.autoConnectActive) {
        epicsTimeStamp now;

        epicsTimeGetCurrent(&now);
        if(epicsTimeDiffInSeconds(
            &now,&pdevice->dpc.lastConnectDisconnect) < 2.0) return FALSE;
        epicsTimeGetCurrent(&pdevice->dpc.lastConnectDisconnect);
        pdevice->dpc.autoConnectActive = TRUE;
        epicsMutexUnlock(pport->asynManagerLock);
        connectAttempt(&pdevice->dpc);
        epicsMutexMustLock(pport->asynManagerLock);
        pdevice->dpc.autoConnectActive = FALSE;
    }
    return pdevice->dpc.connected;
}

static void connectAttempt(dpCommon *pdpCommon)
{
    port           *pport = pdpCommon->pport;
    device         *pdevice = pdpCommon->pdevice;
    asynUser       *pasynUser = pport->pasynUser;
    asynInterface  *pasynInterface;
    asynCommon     *pasynCommon = 0;
    void           *drvPvt = 0;
    asynStatus     status;
    int            addr;

    addr = (pdevice ? pdevice->addr : -1);
    status = pasynManager->connectDevice(pasynUser,pport->portName,addr);
    if(status!=asynSuccess) {
        reportConnectStatus(pport, portConnectDevice,
            "%s %d autoConnect connectDevice failed.\n",
            pport->portName,addr);
        return;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,TRUE);
    if(!pasynInterface) {
        reportConnectStatus(pport, portConnectFindInterface,
            "%s %d autoConnect findInterface for asynCommon failed.\n",
            pport->portName,addr);
            goto disconnect;
    }
    pasynCommon = (asynCommon *)pasynInterface->pinterface;
    drvPvt = pasynInterface->drvPvt;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d autoConnect\n",pport->portName,addr);
    pasynUser->errorMessage[0] = '\0';
    /* When we were called we were not connected, but we could have connected since that test? */
    if (!pdpCommon->connected) {
        epicsMutexMustLock(pport->synchronousLock);
        status = pasynCommon->connect(drvPvt,pasynUser);
        epicsMutexUnlock(pport->synchronousLock);
        if (status != asynSuccess) {
            reportConnectStatus(pport, portConnectDriver,
                "%s %d autoConnect could not connect: %s\n", pport->portName, addr, pasynUser->errorMessage);
        } else {
            reportConnectStatus(pport, portConnectSuccess,
                "%s %d port is now connected\n",
                pport->portName, addr);
        }
    }
disconnect:
    status = pasynManager->disconnect(pasynUser);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "%s %d autoConnect disconnect failed.\n",
            pport->portName,addr);
    }
}

static void portThread(port *pport)
{
    userPvt  *puserPvt;
    asynUser *pasynUser;
    double   timeout;
    BOOL     callTimeoutUser = FALSE;

    taskwdInsert(epicsThreadGetIdSelf(),0,0);
    while(1) {
        epicsEventMustWait(pport->notifyPortThread);
        epicsMutexMustLock(pport->asynManagerLock);
        if(!pport->dpc.enabled) {
            epicsMutexUnlock(pport->asynManagerLock);
            continue;
        }
        /*Process ALL connect/disconnect requests first*/
        while((puserPvt = (userPvt *)ellFirst(
        &pport->queueList[asynQueuePriorityConnect]))) {
            asynStatus status = asynSuccess;

            assert(puserPvt->isQueued);
            ellDelete(&pport->queueList[asynQueuePriorityConnect],
               &puserPvt->node);
            puserPvt->isQueued = FALSE;
            pasynUser = userPvtToAsynUser(puserPvt);
            pasynUser->errorMessage[0] = '\0';
            asynPrint(pasynUser,ASYN_TRACE_FLOW,
                "asynManager connect queueCallback port:%s\n",
                 pport->portName);
            puserPvt->state = callbackActive;
            timeout = puserPvt->timeout;
            epicsMutexUnlock(pport->asynManagerLock);
            if(puserPvt->timer && timeout>0.0) epicsTimerCancel(puserPvt->timer);
            epicsMutexMustLock(pport->synchronousLock);
            if(pport->pasynLockPortNotify) {
                status = pport->pasynLockPortNotify->lock(
                   pport->lockPortNotifyPvt,pasynUser);
                if(status!=asynSuccess) asynPrint(pasynUser,ASYN_TRACE_ERROR,
                        "%s queueCallback pasynLockPortNotify:lock error %s\n",
                         pport->portName,pasynUser->errorMessage);
            }
            puserPvt->processUser(pasynUser);
            if(pport->pasynLockPortNotify) {
                status = pport->pasynLockPortNotify->unlock(
                   pport->lockPortNotifyPvt,pasynUser);
                if(status!=asynSuccess) asynPrint(pasynUser,ASYN_TRACE_ERROR,
                        "%s queueCallback pasynLockPortNotify:lock error %s\n",
                         pport->portName,pasynUser->errorMessage);
            }
            epicsMutexUnlock(pport->synchronousLock);
            epicsMutexMustLock(pport->asynManagerLock);
            if (puserPvt->state==callbackCanceled)
                epicsEventSignal(puserPvt->callbackDone);
            puserPvt->state = callbackIdle;
            if(puserPvt->freeAfterCallback) {
                puserPvt->freeAfterCallback = FALSE;
                epicsMutexMustLock(pasynBase->lock);
                ellAdd(&pasynBase->asynUserFreeList,&puserPvt->node);
                epicsMutexUnlock(pasynBase->lock);
            }
        }
        if(!pport->dpc.connected) {
            if(!autoConnectDevice(pport,0)) {
                epicsMutexUnlock(pport->asynManagerLock);
                continue; /*while (1); */
            }
        }
        while(1) {
            int i;
            dpCommon *pdpCommon = 0;
            asynStatus status = asynSuccess;

            callTimeoutUser = FALSE;
            pport->queueStateChange = FALSE;
            for(i=asynQueuePriorityHigh; i>=asynQueuePriorityLow; i--) {
                for(puserPvt = (userPvt *)ellFirst(&pport->queueList[i]);
                puserPvt; puserPvt = (userPvt *)ellNext(&puserPvt->node)) {
                    pdpCommon = findDpCommon(puserPvt);
                    assert(pdpCommon);

                    if(!pdpCommon->enabled) continue;
                    if(!pdpCommon->connected) {
                        autoConnectDevice(pdpCommon->pport,
                            pdpCommon->pdevice);
                        if(pport->queueStateChange) {
                            puserPvt = 0;
                            break;
                        }
                    }
                    if(!pdpCommon->connected && puserPvt->timeoutUser!=0) {
                       callTimeoutUser = TRUE;
                    }
                    if((pport->pblockProcessHolder==NULL
                        || pport->pblockProcessHolder==puserPvt)
                    && (pdpCommon->pblockProcessHolder==NULL
                        || pdpCommon->pblockProcessHolder==puserPvt)) {
                        assert(puserPvt->isQueued);
                        ellDelete(&pport->queueList[i],&puserPvt->node);
                        puserPvt->isQueued = FALSE;
                        break;
                    }
                }
                if(puserPvt || pport->queueStateChange) break; /*for*/
            }
            if(!puserPvt) break; /*while(1)*/
            pasynUser = userPvtToAsynUser(puserPvt);
            pasynUser->errorMessage[0] = '\0';
            asynPrint(pasynUser,ASYN_TRACE_FLOW,"asynManager::portThread port=%s callback\n",pport->portName);
            puserPvt->state = callbackActive;
            timeout = puserPvt->timeout;
            epicsMutexUnlock(pport->asynManagerLock);
            if(puserPvt->timer && timeout>0.0) epicsTimerCancel(puserPvt->timer);
            epicsMutexMustLock(pport->synchronousLock);
            if(pport->pasynLockPortNotify) {
                status = pport->pasynLockPortNotify->lock(
                   pport->lockPortNotifyPvt,pasynUser);
                if(status!=asynSuccess) asynPrint(pasynUser,ASYN_TRACE_ERROR,
                        "%s queueCallback pasynLockPortNotify:lock error %s\n",
                         pport->portName,pasynUser->errorMessage);
            }
            if(callTimeoutUser) {
                puserPvt->timeoutUser(pasynUser);
            } else {
                puserPvt->processUser(pasynUser);
            }
            if(pport->pasynLockPortNotify) {
                status = pport->pasynLockPortNotify->unlock(
                   pport->lockPortNotifyPvt,pasynUser);
                if(status!=asynSuccess) asynPrint(pasynUser,ASYN_TRACE_ERROR,
                        "%s queueCallback pasynLockPortNotify:lock error %s\n",
                         pport->portName,pasynUser->errorMessage);
            }    
            epicsMutexUnlock(pport->synchronousLock);
            epicsMutexMustLock(pport->asynManagerLock);
            if(puserPvt->blockPortCount>0)
                pport->pblockProcessHolder = puserPvt;
            if(puserPvt->blockDeviceCount>0)
                pdpCommon->pblockProcessHolder = puserPvt;
            if(puserPvt->state==callbackCanceled)
                epicsEventSignal(puserPvt->callbackDone);
            puserPvt->state = callbackIdle;
            if(puserPvt->freeAfterCallback) {
                puserPvt->freeAfterCallback = FALSE;
                epicsMutexMustLock(pasynBase->lock);
                ellAdd(&pasynBase->asynUserFreeList,&puserPvt->node);
                epicsMutexUnlock(pasynBase->lock);
            }
            if(pport->queueStateChange) break;
        }
        epicsMutexUnlock(pport->asynManagerLock);
    }
}
static void queueLockPortCallback(asynUser *pasynUser)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    port     *pport = puserPvt->pport;
    queueLockPortPvt *plockPortPvt = pasynUser->userPvt;

    /* Signal the epicsEvent to let the waiting thread we have been called */
    asynPrint(pasynUser, ASYN_TRACE_FLOW, 
        "%s asynManager::queueLockPortCallback signaling begin event\n", 
        pport->portName);
    epicsEventSignal(plockPortPvt->queueLockPortEvent);
    /* Wait for mutex from queueUnlockPort */
    asynPrint(pasynUser, ASYN_TRACE_FLOW, 
        "%s asynManager::queueLockPortCallback waiting for mutex from queueUnlockPort\n", 
        pport->portName);
    epicsMutexMustLock(plockPortPvt->queueLockPortMutex);
    epicsMutexUnlock(plockPortPvt->queueLockPortMutex);
    asynPrint(pasynUser, ASYN_TRACE_FLOW, 
        "%s asynManager::queueLockPortCallback got mutex from queueUnlockPort, signaling end event\n", 
        pport->portName);
    epicsEventSignal(plockPortPvt->queueLockPortEvent);
}

static void queueLockPortTimeoutCallback(asynUser *pasynUser)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    port     *pport = puserPvt->pport;
    queueLockPortPvt *plockPortPvt = pasynUser->userPvt;

    asynPrint(pasynUser, ASYN_TRACE_WARNING, 
        "%s asynManager::queueLockPortTimeoutCallback WARNING: queueLockPort timeout\n", 
        pport->portName);

    /* Set the pasynUser->auxStatus to asynTimeout to signal error to caller */
    pasynUser->auxStatus = asynTimeout;

    /* Signal the epicsEvent to let the waiting thread we have been called */
    asynPrint(pasynUser, ASYN_TRACE_FLOW, 
        "%s asynManager::queueLockPortTimeoutCallback signaling begin event\n", 
        pport->portName);
    epicsEventSignal(plockPortPvt->queueLockPortEvent);
    
    /* queueUnlockPort is not going to be called because queueLockPort will have returned an error */
};

/* asynManager methods */
static void reportPrintInterfaceList(FILE *fp,ELLLIST *plist,const char *title)
{
    interfaceNode *pinterfaceNode = (interfaceNode *)ellFirst(plist);

    if(pinterfaceNode) fprintf(fp,"    %s\n",title);
    while(pinterfaceNode) {
        asynInterface *pasynInterface = pinterfaceNode->pasynInterface;
        fprintf(fp,"        %s pinterface %p drvPvt %p\n",
            pasynInterface->interfaceType, pasynInterface->pinterface,
            pasynInterface->drvPvt);
        pinterfaceNode = (interfaceNode *)ellNext(&pinterfaceNode->node);
    }
}

/* reportPrintPort is done by separate thread so that synchronousLock
*  and asynManagerLock can be properly reported. If report is run by same thread
*  that has mutex then it would be reported no instead of yes
*/
typedef struct printPortArgs {
    epicsEventId done;
    port *pport;
    FILE *fp;
    int  details;
}printPortArgs;
    
static void reportPrintPort(printPortArgs *pprintPortArgs)
{
    epicsEventId done = pprintPortArgs->done;
    port *pport = pprintPortArgs->pport;
    FILE *fp = pprintPortArgs->fp;
    int  details = pprintPortArgs->details;
    int  showDevices = 1;
    int           i;
    dpCommon *pdpc;
    interfaceNode *pinterfaceNode;
    asynCommon    *pasynCommon = 0;
    void          *drvPvt = 0;
    int           nQueued = 0;

    if (details < 0) {
        showDevices = 0;
        details = -details;
    }
    for(i=asynQueuePriorityLow; i<=asynQueuePriorityConnect; i++) 
        nQueued += ellCount(&pport->queueList[i]);
    pdpc = &pport->dpc;
    fprintf(fp,"%s multiDevice:%s canBlock:%s autoConnect:%s\n",
        pport->portName,
        ((pport->attributes&ASYN_MULTIDEVICE) ? "Yes" : "No"),
        ((pport->attributes&ASYN_CANBLOCK) ? "Yes" : "No"),
        (pdpc->autoConnect ? "Yes" : "No"));
    if(details>=1) {
        epicsMutexLockStatus mgrStatus;
        epicsMutexLockStatus syncStatus;
        syncStatus = epicsMutexTryLock(pport->synchronousLock);
        if(syncStatus==epicsMutexLockOK)
             epicsMutexUnlock(pport->synchronousLock);
        mgrStatus = epicsMutexTryLock(pport->asynManagerLock);
        if(mgrStatus==epicsMutexLockOK)
             epicsMutexUnlock(pport->asynManagerLock);
        fprintf(fp,"    enabled:%s connected:%s numberConnects %lu\n",
            (pdpc->enabled ? "Yes" : "No"),
            (pdpc->connected ? "Yes" : "No"),
             pdpc->numberConnects);
        fprintf(fp,"    nDevices %d nQueued %d blocked:%s\n",
            ellCount(&pport->deviceList),
            nQueued,
            (pport->pblockProcessHolder ? "Yes" : "No"));
        fprintf(fp,"    asynManagerLock:%s synchronousLock:%s\n",
            ((mgrStatus==epicsMutexLockOK) ? "No" : "Yes"),
            ((syncStatus==epicsMutexLockOK) ? "No" : "Yes"));
        fprintf(fp,"    exceptionActive:%s "
            "exceptionUsers %d exceptionNotifys %d\n",
            (pdpc->exceptionActive ? "Yes" : "No"),
            ellCount(&pdpc->exceptionUserList),
            ellCount(&pdpc->exceptionNotifyList));
        fprintf(fp,"    traceMask:0x%x traceIOMask:0x%x traceInfoMask:0x%x\n",
            pdpc->trace.traceMask, pdpc->trace.traceIOMask, pdpc->trace.traceInfoMask);
    }
    if(details>=2) {
        reportPrintInterfaceList(fp,&pdpc->interposeInterfaceList,
                             "interposeInterfaceList");
        reportPrintInterfaceList(fp,&pport->interfaceList,"interfaceList");
    }
    if (showDevices) {
        device   *pdevice = (device *)ellFirst(&pport->deviceList);
        while(pdevice) {
            pdpc = &pdevice->dpc;
            if(!pdpc->connected || details>=1) {
                fprintf(fp,"    addr %d",pdevice->addr);
                fprintf(fp," autoConnect %s enabled %s "
                    "connected %s exceptionActive %s\n",
                    (pdpc->autoConnect ? "Yes" : "No"),
                    (pdpc->enabled ? "Yes" : "No"),
                    (pdpc->connected ? "Yes" : "No"),
                    (pdpc->exceptionActive ? "Yes" : "No"));
            }
            if(details>=1) {
                fprintf(fp,"        exceptionActive %s exceptionUsers %d exceptionNotifys %d\n",
                    (pdpc->exceptionActive ? "Yes" : "No"),
                    ellCount(&pdpc->exceptionUserList),
                    ellCount(&pdpc->exceptionNotifyList));
                fprintf(fp,"        blocked %s\n",
                    (pdpc->pblockProcessHolder ? "Yes" : "No"));
                fprintf(fp,"        traceMask:0x%x traceIOMask:0x%x traceInfoMask:0x%x\n",
                    pdpc->trace.traceMask, pdpc->trace.traceIOMask, pdpc->trace.traceInfoMask);
            }
            if(details>=2) {
                reportPrintInterfaceList(fp,&pdpc->interposeInterfaceList,
                                     "interposeInterfaceList");
            }
            pdevice = (device *)ellNext(&pdevice->node);
        }
    }
    pinterfaceNode = (interfaceNode *)ellFirst(&pport->interfaceList);
    while(pinterfaceNode) {
        asynInterface *pasynInterface = pinterfaceNode->pasynInterface;
        if(strcmp(pasynInterface->interfaceType,asynCommonType)==0) {
            pasynCommon = (asynCommon *)pasynInterface->pinterface;
            drvPvt = pasynInterface->drvPvt;
            break;
        }
        pinterfaceNode = (interfaceNode *)ellNext(&pinterfaceNode->node);
    }
    if(pasynCommon) {
        pasynCommon->report(drvPvt,fp,details);
    }
#ifdef CYGWIN32
    /* This is a (hopefully) temporary fix for a problem with POSIX threads on Cygwin.
     * If a thread is very short-lived, which this report thread will be if the amount of
     * output is small, then it crashes when it exits.  The workaround is a short delay.
     * This should should be fixed in base/src/osi/os/posix/osdThread.c */
    epicsThreadSleep(.001);
#endif
    epicsEventSignal(done);
}

static void report(FILE *fp,int details,const char *portName)
{
    port *pport;
    printPortArgs args;
    epicsEventId done = epicsEventMustCreate(epicsEventEmpty);
    args.done = done;
    args.fp = fp;
    args.details = details;


    if(!pasynBase) asynInit();
    if (portName) {
        pport = locatePort(portName);
        if(!pport) {
            fprintf(fp, "asynManager:report port %s not found\n",portName);
            return;
        }
        args.pport = pport;
        epicsThreadCreate("reportPort",
           epicsThreadPriorityLow,
           epicsThreadGetStackSize(epicsThreadStackSmall),
           (EPICSTHREADFUNC)reportPrintPort,&args);
        epicsEventMustWait(done);
    } else {
        pport = (port *)ellFirst(&pasynBase->asynPortList);
        while(pport) {
            args.pport = pport;
            epicsThreadCreate("reportPort",
               epicsThreadPriorityLow,
               epicsThreadGetStackSize(epicsThreadStackSmall),
               (EPICSTHREADFUNC)reportPrintPort,&args);
            epicsEventMustWait(done);
            pport = (port *)ellNext(&pport->node);
        }
    }
    epicsEventDestroy(done);
}

static asynUser *createAsynUser(userCallback process, userCallback timeout)
{
    userPvt  *puserPvt;
    asynUser *pasynUser;
    int      nbytes;

    if(!pasynBase) asynInit();
    epicsMutexMustLock(pasynBase->lock);
    puserPvt = (userPvt *)ellFirst(&pasynBase->asynUserFreeList);
    if(!puserPvt) {
        epicsMutexUnlock(pasynBase->lock);
        nbytes = sizeof(userPvt) + ERROR_MESSAGE_SIZE + 1;
        puserPvt = callocMustSucceed(1,nbytes,"asynCommon:registerDriver");
        puserPvt->timer = epicsTimerQueueCreateTimer(
            pasynBase->timerQueue,queueTimeoutCallback,puserPvt);
        puserPvt->callbackDone = epicsEventMustCreate(epicsEventEmpty);
        pasynUser = userPvtToAsynUser(puserPvt);
        pasynUser->errorMessage = (char *)(puserPvt +1);
        pasynUser->errorMessageSize = ERROR_MESSAGE_SIZE;
    } else {
        ellDelete(&pasynBase->asynUserFreeList,&puserPvt->node);
        epicsMutexUnlock(pasynBase->lock);
        pasynUser = userPvtToAsynUser(puserPvt);
    }
    puserPvt->processUser = process;
    puserPvt->timeoutUser = timeout;
    puserPvt->timeout = 0.0;
    puserPvt->state = callbackIdle;
    assert(puserPvt->blockPortCount==0);
    assert(puserPvt->blockDeviceCount==0);
    assert(puserPvt->freeAfterCallback==FALSE);
    assert(puserPvt->pexceptionUser==0);
    puserPvt->isQueued = FALSE;
    pasynUser->errorMessage[0] = 0;
    pasynUser->timeout = 0.0;
    pasynUser->userPvt = 0;
    pasynUser->userData = 0;
    pasynUser->drvUser = 0;
    pasynUser->reason = 0;
    pasynUser->auxStatus = 0;
    return pasynUser;
}

static asynUser *duplicateAsynUser(asynUser *pasynUser,
   userCallback process, userCallback timeout)
{
    userPvt *pold = asynUserToUserPvt(pasynUser);
    userPvt *pnew = asynUserToUserPvt(createAsynUser(process,timeout));

    pnew->pport = pold->pport;
    pnew->pdevice = pold->pdevice;
    pnew->user.timeout       = pold->user.timeout;
    pnew->user.userPvt       = pold->user.userPvt;
    pnew->user.userData      = pold->user.userData;
    pnew->user.drvUser       = pold->user.drvUser;
    pnew->user.reason        = pold->user.reason;
    pnew->user.timestamp     = pold->user.timestamp;
    pnew->user.auxStatus     = pold->user.auxStatus;
    pnew->user.alarmStatus   = pold->user.alarmStatus;
    pnew->user.alarmSeverity = pold->user.alarmSeverity;
    return &pnew->user;
}

static asynStatus freeAsynUser(asynUser *pasynUser)
{
    userPvt *puserPvt = asynUserToUserPvt(pasynUser);
    asynStatus status;

    if(puserPvt->pport) {
        status = disconnect(pasynUser);
        if(status!=asynSuccess) return asynError;
    }
    epicsMutexMustLock(pasynBase->lock);
    if(puserPvt->state==callbackIdle) {
        ellAdd(&pasynBase->asynUserFreeList,&puserPvt->node);
    } else {
        puserPvt->freeAfterCallback = TRUE;
    }
    epicsMutexUnlock(pasynBase->lock);
    return asynSuccess;
}

static void *memMalloc(size_t size)
{
    int ind;
    ELLLIST *pmemList;
    memNode *pmemNode;
    
    if(!pasynBase) asynInit();
    for(ind=0; ind<nMemList; ind++) {
        if(size<=memListSize[ind]) break;
    }
    if(ind>=nMemList) {
        return mallocMustSucceed(size,"asynManager::memMalloc");
    }
    pmemList = &pasynBase->memList[ind];
    epicsMutexMustLock(pasynBase->lock);
    pmemNode = (memNode *)ellFirst(pmemList);
    if(pmemNode) {
         ellDelete(pmemList,&pmemNode->node);
     } else {
        /* Note: pmemNode->memory must be multiple of 16 in order to hold any data type */
        pmemNode = mallocMustSucceed(NODESIZE + memListSize[ind],
             "asynManager::memMalloc");
        pmemNode->memory = (char *)pmemNode + NODESIZE;
     }
     epicsMutexUnlock(pasynBase->lock);
     return pmemNode->memory;
}

static void memFree(void *pmem,size_t size)
{
    int ind;
    ELLLIST *pmemList;
    memNode *pmemNode;
    
    assert(size>0);
    if(!pasynBase) asynInit();
    if(size>memListSize[nMemList-1]) {
        free(pmem);
        return;
    }
    for(ind=0; ind<nMemList; ind++) {
        if(size<=memListSize[ind]) break;
    }
    assert(ind<nMemList);
    pmemList = &pasynBase->memList[ind];
    pmemNode = (memNode *)((char *)pmem - NODESIZE);
    assert(pmemNode->memory==pmem);
    epicsMutexMustLock(pasynBase->lock);
    ellAdd(pmemList,&pmemNode->node);
    epicsMutexUnlock(pasynBase->lock);
}

static asynStatus isMultiDevice(asynUser *pasynUser,
    const char *portName,int *yesNo)
{
    port    *pport = locatePort(portName);

    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:isMultiDevice port %s not found",portName);
        return asynError;
    }
    *yesNo = (pport->attributes&ASYN_MULTIDEVICE) ? 1 : 0;
    return asynSuccess;
}

static asynStatus connectDevice(asynUser *pasynUser,
    const char *portName, int addr)
{
    userPvt *puserPvt = asynUserToUserPvt(pasynUser);
    port    *pport = locatePort(portName);
    device  *pdevice;

    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:connectDevice port %s not found",portName);
        return asynError;
    }
    if(puserPvt->pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:connectDevice already connected to device");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    puserPvt->pport = pport;
    if(addr>=0) {
        pdevice = locateDevice(pport,addr,TRUE);
        puserPvt->pdevice = pdevice;
    }
    epicsMutexUnlock(pport->asynManagerLock);
    return asynSuccess;
}

static asynStatus disconnect(asynUser *pasynUser)
{
    userPvt    *puserPvt = asynUserToUserPvt(pasynUser);
    port       *pport = puserPvt->pport;
    asynStatus status = asynSuccess;

    if(!pasynBase) asynInit();
    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::disconnect: not connected");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    if(puserPvt->isQueued) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::disconnect request queued");
        status = asynError; goto unlock;
    }
    if(puserPvt->blockPortCount>0 || puserPvt->blockDeviceCount>0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::disconnect: blockProcessCallback is active");
        status = asynError; goto unlock;
    }
    if(puserPvt->pexceptionUser) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::disconnect: on exceptionCallback list");
        status = asynError; goto unlock;
    }
    puserPvt->pport = 0;
    puserPvt->pdevice = 0;
unlock:
    epicsMutexUnlock(pport->asynManagerLock);
    return status;
}

static asynStatus exceptionCallbackAdd(asynUser *pasynUser,
    exceptionCallback callback)
{
    userPvt       *puserPvt = asynUserToUserPvt(pasynUser);
    port          *pport = puserPvt->pport;
    dpCommon      *pdpCommon = findDpCommon(puserPvt);
    exceptionUser *pexceptionUser;

    if(!callback) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:exceptionCallbackAdd callback was NULL");
        return asynError;
    }
    if(!pport || !pdpCommon) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:exceptionCallbackAdd not connected");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    pexceptionUser = puserPvt->pexceptionUser;
    if(pexceptionUser) {
        epicsMutexUnlock(pport->asynManagerLock);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:exceptionCallbackAdd already on list");
        return asynError;
    }
    pexceptionUser = callocMustSucceed(1,sizeof(exceptionUser),"asynManager");
    pexceptionUser->pasynUser = pasynUser;
    pexceptionUser->callback = callback;
    pexceptionUser->notify = epicsEventMustCreate(epicsEventEmpty);
    while(pdpCommon->exceptionActive) {
        ellAdd(&pdpCommon->exceptionNotifyList,&pexceptionUser->notifyNode);
        epicsMutexUnlock(pport->asynManagerLock);
        epicsEventMustWait(pexceptionUser->notify);
        epicsMutexMustLock(pport->asynManagerLock);
    }
    puserPvt->pexceptionUser = pexceptionUser;
    ellAdd(&pdpCommon->exceptionUserList,&pexceptionUser->node);
    epicsMutexUnlock(pport->asynManagerLock);
    return asynSuccess;
}

static asynStatus exceptionCallbackRemove(asynUser *pasynUser)
{
    userPvt       *puserPvt = asynUserToUserPvt(pasynUser);
    port          *pport = puserPvt->pport;
    dpCommon      *pdpCommon = findDpCommon(puserPvt);
    exceptionUser *pexceptionUser;

    if(!pport || !pdpCommon) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:exceptionCallbackRemove not connected");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    pexceptionUser = puserPvt->pexceptionUser;
    if(!pexceptionUser) {
        epicsMutexUnlock(pport->asynManagerLock);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:exceptionCallbackRemove not on list");
        return asynError;
    }
    while(pdpCommon->exceptionActive) {
        ellAdd(&pdpCommon->exceptionNotifyList,&pexceptionUser->notifyNode);
        epicsMutexUnlock(pport->asynManagerLock);
        epicsEventMustWait(pexceptionUser->notify);
        epicsMutexMustLock(pport->asynManagerLock);
    }
    puserPvt->pexceptionUser = 0;
    ellDelete(&pdpCommon->exceptionUserList,&pexceptionUser->node);
    epicsEventDestroy(pexceptionUser->notify);
    free(pexceptionUser);
    epicsMutexUnlock(pport->asynManagerLock);
    return asynSuccess;
}
   
static asynInterface *findInterface(asynUser *pasynUser,
    const char *interfaceType,int interposeInterfaceOK)
{
    userPvt       *puserPvt = asynUserToUserPvt(pasynUser);
    port          *pport = puserPvt->pport;
    device        *pdevice = puserPvt->pdevice;
    interfaceNode *pinterfaceNode;

    if(!pasynBase) asynInit();
    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:findInterface: not connected");
        return 0;
    }
    if(interposeInterfaceOK) {
        if(pdevice) {
            pinterfaceNode = locateInterfaceNode(
                &pdevice->dpc.interposeInterfaceList, interfaceType,FALSE);
            if(pinterfaceNode) return(pinterfaceNode->pasynInterface);
        }
        pinterfaceNode = locateInterfaceNode(
            &pport->dpc.interposeInterfaceList, interfaceType,FALSE);
        if(pinterfaceNode) return(pinterfaceNode->pasynInterface);
    }
    pinterfaceNode = locateInterfaceNode(
        &pport->interfaceList,interfaceType,FALSE);
    if(pinterfaceNode) return(pinterfaceNode->pasynInterface);
    return 0;
}

static asynStatus queueRequest(asynUser *pasynUser,
    asynQueuePriority priority,double timeout)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    port     *pport = puserPvt->pport;
    device   *pdevice = puserPvt->pdevice;
    int      addr = (pdevice ? pdevice->addr : -1);
    dpCommon *pdpCommon = findDpCommon(puserPvt);
    BOOL     addToFront = FALSE;
    BOOL     checkPortConnect = TRUE;

    assert(priority>=asynQueuePriorityLow && priority<=asynQueuePriorityConnect);
    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::queueRequest asynUser not associated with a port");
        return asynError;
    }
    if(!puserPvt->processUser) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::queueRequest no processCallback");
        return asynError;
    }
    if((priority==asynQueuePriorityConnect)
    && (addr==-1 || pasynUser->reason==ASYN_REASON_QUEUE_EVEN_IF_NOT_CONNECTED)) {
        checkPortConnect = FALSE;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    if(!pport->dpc.enabled) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "port %s disabled",pport->portName);
        epicsMutexUnlock(pport->asynManagerLock);
        return asynDisabled;
    }
    if(checkPortConnect && !pport->dpc.connected) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "port %s not connected",pport->portName);
        epicsMutexUnlock(pport->asynManagerLock);
        return asynDisconnected;
    }
    if(!(pport->attributes&ASYN_CANBLOCK)) {
        device   *pdevice = puserPvt->pdevice;
        int      addr = (pdevice ? pdevice->addr : -1);
        dpCommon *pdpCommon;
        
        pdpCommon = findDpCommon(puserPvt);
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s queueRequest synchronous\n",
            pport->portName);
        if(!pport->dpc.enabled
        || (addr>=0 && !pdpCommon->enabled)) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "port %s  or device %d not enabled",pport->portName,addr);
            epicsMutexUnlock(pport->asynManagerLock);
            return asynDisabled;
        }
        if((!pport->dpc.connected || !pdpCommon->connected)
        && (pasynUser->reason != ASYN_REASON_QUEUE_EVEN_IF_NOT_CONNECTED)) {
            if(priority<asynQueuePriorityConnect
            && !autoConnectDevice(pport,pdevice)) {
                epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                    "port %s or device %d not connected",pport->portName,addr);
                epicsMutexUnlock(pport->asynManagerLock);
                return asynDisconnected;
            }
        }
        epicsMutexUnlock(pport->asynManagerLock);
        epicsMutexMustLock(pport->synchronousLock);
        puserPvt->processUser(pasynUser);
        epicsMutexUnlock(pport->synchronousLock);
        return asynSuccess;
    }
    if(puserPvt->isQueued) {
        epicsMutexUnlock(pport->asynManagerLock);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::queueRequest is already queued");
        return asynError;
    }
    if(timeout>0.0 && !puserPvt->timeoutUser) {
        epicsMutexUnlock(pport->asynManagerLock);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager::queueRequest timeout requested but no "
            "timeout callback was passed to createAsynUser");
        return asynError;
    }
    if(puserPvt->blockPortCount>0 || puserPvt->blockDeviceCount>0) {
        if(pport->pblockProcessHolder
        && pport->pblockProcessHolder==puserPvt) addToFront = TRUE;
        if(pdpCommon->pblockProcessHolder
        && pdpCommon->pblockProcessHolder==puserPvt) addToFront = TRUE;
    }
    if(addToFront) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s addr %d queueRequest priority %d from lockHolder\n",
            pport->portName,addr,priority);
        ellInsert(&pport->queueList[priority],0,&puserPvt->node);
    } else {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s addr %d queueRequest priority %d not lockHolder\n",
            pport->portName,addr,priority);
        /*Add to end of list*/
        ellAdd(&pport->queueList[priority],&puserPvt->node);
    }
    pport->queueStateChange = TRUE;
    puserPvt->isQueued = TRUE;
    if(timeout<=0.0) {
        puserPvt->timeout = 0.0;
    } else {
        puserPvt->timeout = timeout;
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s schedule queueRequest timeout in %f seconds\n",puserPvt->pport->portName,puserPvt->timeout);
        epicsTimerStartDelay(puserPvt->timer,puserPvt->timeout);
    }
    epicsMutexUnlock(pport->asynManagerLock);
    epicsEventSignal(pport->notifyPortThread);
    return asynSuccess;
}

static asynStatus cancelRequest(asynUser *pasynUser,int *wasQueued)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    port     *pport = puserPvt->pport;
    device   *pdevice = puserPvt->pdevice;
    double   timeout;
    int      addr = (pdevice ? pdevice->addr : -1);
    int      i;
    *wasQueued = 0; /*Initialize to not removed*/
    if(!pport) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "asynManager:cancelRequest but not connected\n");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    if(!puserPvt->isQueued) {
        if(puserPvt->state==callbackActive) {
            asynPrint(pasynUser,ASYN_TRACE_FLOW,
                "%s addr %d asynManager:cancelRequest wait for callback\n",
                 pport->portName,addr);
            puserPvt->state = callbackCanceled;
            epicsMutexUnlock(pport->asynManagerLock);
            epicsEventMustWait(puserPvt->callbackDone);
        } else {
            epicsMutexUnlock(pport->asynManagerLock);
            asynPrint(pasynUser,ASYN_TRACE_FLOW,
                "%s addr %d asynManager:cancelRequest but not queued\n",
                 pport->portName,addr);
        }
        return asynSuccess;
    }
    for(i=asynQueuePriorityConnect; i>=asynQueuePriorityLow; i--) {
        puserPvt = (userPvt *)ellFirst(&pport->queueList[i]);
        while(puserPvt) {
            if(pasynUser == &puserPvt->user) {
                ellDelete(&pport->queueList[i],&puserPvt->node);
                *wasQueued = 1;
                break;
            }
            puserPvt = (userPvt *)ellNext(&puserPvt->node);
        }
        if(puserPvt) break;
    }
    if(!puserPvt) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s addr %d asynManager:cancelRequest LOGIC ERROR\n",
            pport->portName, addr);
        epicsMutexUnlock(pport->asynManagerLock);
        return asynError;
    }
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
             "%s addr %d asynManager:cancelRequest\n",
              pport->portName,addr);
    puserPvt->isQueued = FALSE;
    pport->queueStateChange = TRUE;
    timeout = puserPvt->timeout;
    epicsMutexUnlock(pport->asynManagerLock);
    if(puserPvt->timer && timeout>0.0) epicsTimerCancel(puserPvt->timer);
    epicsEventSignal(pport->notifyPortThread);
    return asynSuccess;
}

static asynStatus blockProcessCallback(asynUser *pasynUser, int allDevices)
{
    userPvt *puserPvt = asynUserToUserPvt(pasynUser);
    port    *pport = puserPvt->pport;
    int     can;

    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::blockProcessCallback not connected");
        return asynError;
    }
    canBlock(pasynUser,&can);
    if(!can) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::blockProcessCallback blockProcessCallback "
                "not supported because port is synchronous");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    if(puserPvt->isQueued) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::blockProcessCallback is queued");
        epicsMutexUnlock(pport->asynManagerLock);
        return asynError;
    }
    if(allDevices) 
        puserPvt->blockPortCount++;
    else 
        puserPvt->blockDeviceCount++;
    epicsMutexUnlock(pport->asynManagerLock);
    return asynSuccess;
}

static asynStatus unblockProcessCallback(asynUser *pasynUser, int allDevices)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    port     *pport = puserPvt->pport;
    BOOL     wasOwner = FALSE;

    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::unblockProcessCallback not connected");
        return asynError;
    }
    if(( allDevices && (puserPvt->blockPortCount == 0))
    || (!allDevices && (puserPvt->blockDeviceCount == 0))) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::unblockProcessCallback but not locked");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    if(puserPvt->isQueued) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::unblockProcessCallback is queued");
        epicsMutexUnlock(pport->asynManagerLock);
        return asynError;
    }
    if((allDevices&&puserPvt->blockPortCount==0)
    || (!allDevices && puserPvt->blockDeviceCount==0)) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::unblockProcessCallback but not blocked");
        epicsMutexUnlock(pport->asynManagerLock);
        return asynError;
    }
    if(allDevices) {
        puserPvt->blockPortCount--;
        if(puserPvt->blockPortCount==0
        && pport->pblockProcessHolder==puserPvt) {
            pport->pblockProcessHolder = 0;
            wasOwner = TRUE;
        }
    } else if (--puserPvt->blockDeviceCount==0) {
        dpCommon *pdpCommon = findDpCommon(puserPvt);

        if (pdpCommon->pblockProcessHolder==puserPvt) {
            pdpCommon->pblockProcessHolder = 0;
            wasOwner = TRUE;
        }
    }
    epicsMutexUnlock(pport->asynManagerLock);
    if(wasOwner) epicsEventSignal(pport->notifyPortThread);
    return asynSuccess;
}

static asynStatus lockPort(asynUser *pasynUser)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    port     *pport = puserPvt->pport;

    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::lockPort not connected\n");
        return asynError;
    }
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s lockPort\n", pport->portName);
    epicsMutexMustLock(pport->synchronousLock);
    if(pport->pasynLockPortNotify) {
        pport->pasynLockPortNotify->lock(
           pport->lockPortNotifyPvt,pasynUser);
    }
    return asynSuccess;
}

static asynStatus unlockPort(asynUser *pasynUser)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    port     *pport = puserPvt->pport;

    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::unlockPort not connected\n");
        return asynError;
    }
    asynPrint(pasynUser,ASYN_TRACE_FLOW, "%s unlockPort\n", pport->portName);
    if(pport->pasynLockPortNotify) {
        asynStatus status;
        status = pport->pasynLockPortNotify->unlock(
           pport->lockPortNotifyPvt,pasynUser);
        if(status!=asynSuccess) {
            epicsMutexUnlock(pport->synchronousLock);
            return status;
        }
    }
    epicsMutexUnlock(pport->synchronousLock);
    return asynSuccess;
}

static asynStatus queueLockPort(asynUser *pasynUser)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    port     *pport = puserPvt->pport;
    queueLockPortPvt *plockPortPvt;
    asynUser *pasynUserCopy;
    asynStatus status = asynSuccess;
    double timeout;

    asynPrint(pasynUser,ASYN_TRACE_FLOW, "%s asynManager::queueLockPort locking port\n", pport->portName);
    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::queueLockPort not connected");
        return asynError;
    }
    if (pport->attributes & ASYN_CANBLOCK) {   /* Asynchronous driver */
        plockPortPvt = epicsThreadPrivateGet(pport->queueLockPortId);
        if (!plockPortPvt) {
            /* This is the first time queueLockPort has been called for this thread */
            plockPortPvt = callocMustSucceed(1,sizeof(queueLockPortPvt),"asynManager::queueLockPort");
            asynPrint(pasynUser,ASYN_TRACE_FLOW, "%s asynManager::queueLockPort created queueLockPortPvt=%p\n", 
                pport->portName, plockPortPvt);
            plockPortPvt->queueLockPortEvent = epicsEventMustCreate(epicsEventEmpty);
            plockPortPvt->queueLockPortMutex = epicsMutexMustCreate();
            plockPortPvt->queueLockPortCount = 0;
            epicsThreadPrivateSet(pport->queueLockPortId, plockPortPvt);
            asynPrint(pasynUser,ASYN_TRACE_FLOW, "%s asynManager::queueLockPort created queueLockPortPvt=%p, event=%p, mutex=%p\n", 
                pport->portName, plockPortPvt, plockPortPvt->queueLockPortEvent, plockPortPvt->queueLockPortMutex);
        }
        /* If queueLockPort has been called recursively, we already have the lock. */
        /* Thus, we just increase the counter and return. */
        if (plockPortPvt->queueLockPortCount > 0) {
            plockPortPvt->queueLockPortCount++;
            return status;
        }
        pasynUserCopy = pasynManager->duplicateAsynUser(pasynUser, queueLockPortCallback, queueLockPortTimeoutCallback);
        if (!pasynUserCopy){
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                    "asynManager::queueLockPort duplicateAsynUser failed");
            return asynError;
        }
        pasynUserCopy->userPvt = plockPortPvt;
        /* Set the auxStatus field to asynSuccess.  If it is not asynSuccess later we know queueRequest timed out */
        pasynUserCopy->auxStatus = asynSuccess;
        /* Take the mutex which will block the callback port thread */
        /* Wait until the queued request executes the callback */
        asynPrint(pasynUser,ASYN_TRACE_FLOW, "%s asynManager::queueLockPort taking mutex %p\n", 
            pport->portName, plockPortPvt->queueLockPortMutex);
        epicsMutexMustLock(plockPortPvt->queueLockPortMutex);
        asynPrint(pasynUser,ASYN_TRACE_FLOW, "%s asynManager::queueLockPort queueing request\n", pport->portName);
        timeout = pport->queueLockPortTimeout;
        if (pasynUserCopy->timeout > timeout) timeout = pasynUserCopy->timeout;
        status = pasynManager->queueRequest(pasynUserCopy, asynQueuePriorityLow, timeout);
        if (status) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::queueLockPort queueRequest failed: %s", 
                pasynUserCopy->errorMessage);
            epicsMutexUnlock(plockPortPvt->queueLockPortMutex);
            pasynManager->freeAsynUser(pasynUserCopy);
            return asynError;
        }
        /* Wait for event from the port thread in the queueLockPortCallback function */
        asynPrint(pasynUser,ASYN_TRACE_FLOW, "%s asynManager::queueLockPort waiting for event\n", pport->portName);
        epicsEventMustWait(plockPortPvt->queueLockPortEvent);
        /* Check the auxStatus value */
        if (pasynUserCopy->auxStatus != asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::queueLockPort queueRequest timed out");
            epicsMutexUnlock(plockPortPvt->queueLockPortMutex);
            pasynManager->freeAsynUser(pasynUserCopy);
            return asynError;
        }
        pasynManager->freeAsynUser(pasynUserCopy);
        asynPrint(pasynUser,ASYN_TRACE_FLOW, "%s asynManager::queueLockPort got event from callback\n", pport->portName);
        /* Remember that the port is locked by this thread. */
        plockPortPvt->queueLockPortCount++;
    } else {
        /* Synchronous driver */
        epicsMutexMustLock(pport->synchronousLock);
    }
    if(pport->pasynLockPortNotify) {
        status = pport->pasynLockPortNotify->lock(
           pport->lockPortNotifyPvt,pasynUser);
    }
    return status;
}

static asynStatus queueUnlockPort(asynUser *pasynUser)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    port     *pport = puserPvt->pport;
    queueLockPortPvt *plockPortPvt;
    asynStatus status = asynSuccess;

    asynPrint(pasynUser,ASYN_TRACE_FLOW, "%s queueUnlockPort\n", pport->portName);
    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::queueUnlockPort not connected");
        return asynError;
    }
    if(pport->pasynLockPortNotify) {
        status = pport->pasynLockPortNotify->unlock(
           pport->lockPortNotifyPvt,pasynUser);
    }
    if (pport->attributes & ASYN_CANBLOCK) {   /* Asynchronous driver */
        plockPortPvt = epicsThreadPrivateGet(pport->queueLockPortId);
        if (!plockPortPvt) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                    "asynManager::queueUnlockPort queueLockPort never called for this thread");
            return asynError;
        }
        /* It is an error to call queueUnlockPort() if the port is not locked. */
        if (plockPortPvt->queueLockPortCount == 0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                    "asynManager::queueUnlockPort port is not locked by calling thread");
            return asynError;
        }
        /* If the counter is greater than one, queueLockPort() has been called multiple times. */
        /* Thus, we do not release the lock but just decrease the counter and return. */
        if (plockPortPvt->queueLockPortCount > 1) {
            plockPortPvt->queueLockPortCount--;
            return status;
        }
        epicsMutexUnlock(plockPortPvt->queueLockPortMutex);
        /* Wait for event from the port thread in the queueLockPortCallback function which signals it has freed mutex */
        asynPrint(pasynUser,ASYN_TRACE_FLOW, "%s asynManager::queueUnlockPort waiting for event\n", pport->portName);
        epicsEventMustWait(plockPortPvt->queueLockPortEvent);
        asynPrint(pasynUser,ASYN_TRACE_FLOW, "%s queueUnlockPort unlock mutex %p complete.\n", 
            pport->portName, plockPortPvt->queueLockPortMutex);
        /* Remember the port is not locked any longer by this thread. */
        plockPortPvt->queueLockPortCount--;
    } else {
        /* Synchronous driver */
        epicsMutexUnlock(pport->synchronousLock);
    }    
    return status;
}

static asynStatus canBlock(asynUser *pasynUser,int *yesNo)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    port     *pport = puserPvt->pport;

    if(!pport) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "canBlock but not connected\n");
        return asynError;
    }
    *yesNo = (pport->attributes&ASYN_CANBLOCK) ? 1 : 0;
    return asynSuccess;
}

static asynStatus getAddr(asynUser *pasynUser,int *addr)
{
    userPvt *puserPvt = asynUserToUserPvt(pasynUser);
    port    *pport = puserPvt->pport;

    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:getAddr not connected to device");
        return asynError;
    }
    if(!(pport->attributes&ASYN_MULTIDEVICE) || !puserPvt->pdevice) {
        *addr = -1;
    } else {
        *addr = puserPvt->pdevice->addr;
    }
    return asynSuccess;
}

static asynStatus getPortName(asynUser *pasynUser,const char **pportName)
{
    userPvt *puserPvt = asynUserToUserPvt(pasynUser);
    port    *pport = puserPvt->pport;

    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:getPortName not connected to device");
        return asynError;
    }
    *pportName = pport->portName;
    return asynSuccess;
}

static asynStatus registerPort(const char *portName,
    int attributes,int autoConnect,
    unsigned int priority,unsigned int stackSize)
{
    port    *pport = locatePort(portName);
    int     i;
    size_t  len;

    if(pport) {
        printf("asynCommon:registerDriver %s already registered\n",portName);
        return asynError;
    }
    len = sizeof(port) + strlen(portName) + 1;
    pport = callocMustSucceed(len,sizeof(char),"asynCommon:registerDriver");
    pport->portName = (char *)(pport + 1);
    strcpy(pport->portName,portName);
    pport->attributes = attributes;
    pport->asynManagerLock = epicsMutexMustCreate();
    pport->synchronousLock = epicsMutexMustCreate();
    pport->queueLockPortId = epicsThreadPrivateCreate();
    pport->timeStampSource = defaultTimeStampSource;
    dpCommonInit(pport,0,autoConnect);
    pport->pasynUser = createAsynUser(0,0);
    pport->previousConnectStatus = portConnectSuccess;
    pport->queueLockPortTimeout = DEFAULT_QUEUE_LOCK_PORT_TIMEOUT;
    ellInit(&pport->deviceList);
    ellInit(&pport->interfaceList);
    if((attributes&ASYN_CANBLOCK)) {
        for(i=0; i<NUMBER_QUEUE_PRIORITIES; i++) ellInit(&pport->queueList[i]);
        pport->notifyPortThread = epicsEventMustCreate(epicsEventEmpty);
        priority = priority ? priority : epicsThreadPriorityMedium;
        stackSize = stackSize ?
                       stackSize :
                       epicsThreadGetStackSize(epicsThreadStackMedium);
        pport->threadid = epicsThreadCreate(portName,priority,stackSize,        
             (EPICSTHREADFUNC)portThread,pport);
        if(!pport->threadid){
            printf("asynCommon:registerDriver %s epicsThreadCreate failed \n",
                portName);
            epicsEventDestroy(pport->notifyPortThread);
            freeAsynUser(pport->pasynUser);
            dpCommonFree(&pport->dpc);
            epicsMutexDestroy(pport->synchronousLock);
            epicsMutexDestroy(pport->asynManagerLock);
            free(pport);
            return asynError;
        }
    }
    epicsMutexMustLock(pasynBase->lock);
    ellAdd(&pasynBase->asynPortList,&pport->node);
    epicsMutexUnlock(pasynBase->lock);
    return asynSuccess;
}

static asynStatus registerInterface(const char *portName,
    asynInterface *pasynInterface)
{
    port          *pport = locatePort(portName);
    interfaceNode *pinterfaceNode;

    if(!pport) {
       printf("asynManager:registerInterface portName %s not registered\n",
          portName);
       return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    if(strcmp(pasynInterface->interfaceType,asynLockPortNotifyType)==0) {
        pport->pasynLockPortNotify =
            (asynLockPortNotify *)pasynInterface->pinterface;
        pport->lockPortNotifyPvt = pasynInterface->drvPvt;
        epicsMutexUnlock(pport->asynManagerLock);
        return asynSuccess;
    }
    pinterfaceNode = locateInterfaceNode(
        &pport->interfaceList,pasynInterface->interfaceType,TRUE);
    if(pinterfaceNode->pasynInterface) {
        printf("interface %s already registered for port %s\n",
            pasynInterface->interfaceType,pport->portName);
        epicsMutexUnlock(pport->asynManagerLock);
        return asynError;
    }
    pinterfaceNode->pasynInterface = pasynInterface;
    epicsMutexUnlock(pport->asynManagerLock);
    if(strcmp(pasynInterface->interfaceType,asynCommonType)==0) {
        initPortConnect(pport);
        portConnectTimerCallback(pport);
        /* Wait for the default time for the port to connect */
        if (pport->dpc.autoConnect) waitConnect(pport->pconnectUser, pasynBase->autoConnectTimeout);
    }
    return asynSuccess;
}

static asynStatus exceptionConnect(asynUser *pasynUser)
{
    userPvt    *puserPvt = asynUserToUserPvt(pasynUser);
    port       *pport = puserPvt->pport;
    dpCommon   *pdpCommon = findDpCommon(puserPvt);

    if(!pport || !pdpCommon) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:exceptionConnect not connected to port/device");
        return asynError;
    }
    if(pdpCommon->connected) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s addr %d asynManager:exceptionConnect already connected",
            pport->portName, (puserPvt->pdevice ? puserPvt->pdevice->addr : -1));
        return asynError;
    }
    pdpCommon->connected = TRUE;
    ++pdpCommon->numberConnects;
    exceptionOccurred(pasynUser,asynExceptionConnect);
    return asynSuccess;
}

static asynStatus exceptionDisconnect(asynUser *pasynUser)
{
    userPvt    *puserPvt = asynUserToUserPvt(pasynUser);
    port       *pport = puserPvt->pport;
    dpCommon   *pdpCommon = findDpCommon(puserPvt);

    if(!pport || !pdpCommon) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:exceptionDisconnect not connected");
        return asynError;
    }
    if(!pdpCommon->connected) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s addr %d asynManager:exceptionDisconnect but not connected",
            pport->portName, (puserPvt->pdevice ? puserPvt->pdevice->addr : -1));
        return asynError;
    }
    pdpCommon->connected = FALSE;
    if(!pport->dpc.connected && pport->dpc.autoConnect) {
        epicsTimerStartDelay(pport->connectTimer,.01);
    }
    epicsTimeGetCurrent(&pdpCommon->lastConnectDisconnect);
    exceptionOccurred(pasynUser,asynExceptionConnect);
    return asynSuccess;
    
}

static asynStatus interposeInterface(const char *portName, int addr,
    asynInterface *pasynInterface,asynInterface **ppPrev)
{
    port          *pport = locatePort(portName);
    device        *pdevice;
    interfaceNode *pinterfaceNode;
    interfaceNode *pinterfaceNodePort;
    asynInterface *pPrev = 0;
    dpCommon      *pdpCommon = 0;

    if(!pport) return asynError;
    epicsMutexMustLock(pport->asynManagerLock);
    if(addr>=0) {
        pdevice = locateDevice(pport,addr,TRUE);
        if(pdevice) pdpCommon = &pdevice->dpc;
    }
    if(!pdpCommon) pdpCommon = &pport->dpc;
    pinterfaceNode = locateInterfaceNode(&pdpCommon->interposeInterfaceList,
        pasynInterface->interfaceType,TRUE);
    if(pinterfaceNode->pasynInterface) {
        pPrev = pinterfaceNode->pasynInterface;
    } else {
        pinterfaceNodePort = locateInterfaceNode(&pport->interfaceList,
            pasynInterface->interfaceType,FALSE);
        if(pinterfaceNodePort) pPrev = pinterfaceNodePort->pasynInterface;
    }
    if(ppPrev) *ppPrev = pPrev;
    pinterfaceNode->pasynInterface = pasynInterface;
    epicsMutexUnlock(pport->asynManagerLock);
    return asynSuccess;
}

static asynStatus enable(asynUser *pasynUser,int yesNo)
{
    userPvt    *puserPvt = asynUserToUserPvt(pasynUser);
    port       *pport = puserPvt->pport;
    dpCommon   *pdpCommon = findDpCommon(puserPvt);

    if(!pport || !pdpCommon) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:enable not connected");
        return asynError;
    }
    pdpCommon->enabled = (yesNo ? 1 : 0);
    exceptionOccurred(pasynUser,asynExceptionEnable);
    return asynSuccess;
}

static asynStatus autoConnectAsyn(asynUser *pasynUser,int yesNo)
{
    userPvt    *puserPvt = asynUserToUserPvt(pasynUser);
    port       *pport = puserPvt->pport;
    dpCommon   *pdpCommon = findDpCommon(puserPvt);

    if(!pport || !pdpCommon) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:autoConnect not connected");
        return asynError;
    }
    pdpCommon->autoConnect = (yesNo ? 1 : 0);
    exceptionOccurred(pasynUser,asynExceptionAutoConnect);
    return asynSuccess;
}

static asynStatus isConnected(asynUser *pasynUser,int *yesNo)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    dpCommon *pdpCommon = findDpCommon(puserPvt);

    if(!pdpCommon) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:isConnected asynUser not connected to device");
        return asynError;
    }
    *yesNo = pdpCommon->connected;
    return asynSuccess;
}

static asynStatus isEnabled(asynUser *pasynUser,int *yesNo)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    dpCommon *pdpCommon = findDpCommon(puserPvt);

    if(!pdpCommon) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:isEnabled asynUser not connected to device");
        return asynError;
    }
    *yesNo = pdpCommon->enabled;
    return asynSuccess;
}

static asynStatus isAutoConnect(asynUser *pasynUser,int *yesNo)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    dpCommon *pdpCommon = findDpCommon(puserPvt);

    if(!pdpCommon) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:isAutoConnect asynUser not connected to device");
        return asynError;
    }
    *yesNo = pdpCommon->autoConnect;
    return asynSuccess;
}

static asynStatus setAutoConnectTimeout(double timeout)
{
    if(!pasynBase) asynInit();
    epicsMutexMustLock(pasynBase->lock);
    pasynBase->autoConnectTimeout = timeout;
    epicsMutexUnlock(pasynBase->lock);
    return asynSuccess;
}

static asynStatus setQueueLockPortTimeout(asynUser *pasynUser, double timeout)
{
    userPvt    *puserPvt = asynUserToUserPvt(pasynUser);
    port *pport = puserPvt->pport;

    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:setQueueLockPortTimeout not connected to device");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    pport->queueLockPortTimeout = timeout;
    epicsMutexUnlock(pport->asynManagerLock);
    return asynSuccess;
}

static asynStatus registerInterruptSource(const char *portName,
    asynInterface *pasynInterface, void **pasynPvt)
{
    port          *pport = locatePort(portName);
    interfaceNode *pinterfaceNode;
    interruptBase *pinterruptBase;

    if(!pport) {
       printf("asynManager:registerInterruptSource port %s not registered\n",
          portName);
       return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    pinterfaceNode = locateInterfaceNode(
        &pport->interfaceList,pasynInterface->interfaceType,FALSE);
    if(!pinterfaceNode) 
         pinterfaceNode = locateInterfaceNode(
             &pport->dpc.interposeInterfaceList,
             pasynInterface->interfaceType,FALSE);
    if(!pinterfaceNode) {
        epicsMutexUnlock(pport->asynManagerLock);
        printf("%s asynManager:registerInterruptSource interface "
            "not registered\n", portName);
        return asynError;
    }
    if(pinterfaceNode->pinterruptBase) {
        epicsMutexUnlock(pport->asynManagerLock);
        printf("%s asynManager:registerInterruptSource already registered\n",
            pport->portName);
        return asynError;
    }
    pinterruptBase = callocMustSucceed(1,sizeof(interruptBase),
        "asynManager:registerInterruptSource");
    pinterfaceNode->pinterruptBase = pinterruptBase;
    ellInit(&pinterruptBase->callbackList);
    ellInit(&pinterruptBase->addRemoveList);
    pinterruptBase->pasynInterface = pinterfaceNode->pasynInterface;
    pinterruptBase->pport = pport;
    *pasynPvt = pinterruptBase;
    epicsMutexUnlock(pport->asynManagerLock);
    return asynSuccess;
}

static asynStatus getInterruptPvt(asynUser *pasynUser,
    const char *interfaceType, void **pasynPvt)
{
    userPvt    *puserPvt = asynUserToUserPvt(pasynUser);
    port *pport = puserPvt->pport;
    interfaceNode *pinterfaceNode;

    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
            "asynManager:getInterruptPvt not connected to a port");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    pinterfaceNode = locateInterfaceNode(
        &pport->interfaceList,interfaceType,FALSE);
    if(!pinterfaceNode) 
         pinterfaceNode = locateInterfaceNode(
             &pport->dpc.interposeInterfaceList,interfaceType,FALSE);
    if(!pinterfaceNode) {
        epicsMutexUnlock(pport->asynManagerLock);
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
            "asynManager:getInterruptPvt interface %s is not registered",
            interfaceType);
        return asynError;
    }
    *pasynPvt = pinterfaceNode->pinterruptBase;
    epicsMutexUnlock(pport->asynManagerLock);
    if (!*pasynPvt) {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                      "asynManager:getInterruptPvt Driver does not "
                      "support interrupts on interface %s",
                      interfaceType);
        return(asynError);
    }
    return asynSuccess;
}

static interruptNode *createInterruptNode(void *pasynPvt)
{
    interruptBase    *pinterruptBase = (interruptBase *)pasynPvt;
    interruptNode    *pinterruptNode;
    interruptNodePvt *pinterruptNodePvt;

    epicsMutexMustLock(pasynBase->lock);
    pinterruptNode = (interruptNode *)ellFirst(&pasynBase->interruptNodeFree);
    if(pinterruptNode) {
        pinterruptNodePvt = interruptNodeToPvt(pinterruptNode);
        ellDelete(&pasynBase->interruptNodeFree,&pinterruptNode->node);
        epicsMutexUnlock(pasynBase->lock);
        pinterruptNodePvt->isOnList = 0;
        pinterruptNodePvt->isOnAddRemoveList = 0;
        memset(&pinterruptNodePvt->nodePublic,0,sizeof(interruptNode));
    } else {
        epicsMutexUnlock(pasynBase->lock);
        pinterruptNodePvt = (interruptNodePvt *)
            callocMustSucceed(1,sizeof(interruptNodePvt),
                "asynManager:createInterruptNode");
        pinterruptNodePvt->callbackDone = epicsEventMustCreate(epicsEventEmpty);
    }
    pinterruptNodePvt->pinterruptBase = pinterruptBase;
    return(&pinterruptNodePvt->nodePublic);
}

static asynStatus freeInterruptNode(asynUser *pasynUser,interruptNode *pinterruptNode)
{
    interruptNodePvt *pinterruptNodePvt = interruptNodeToPvt(pinterruptNode);
    interruptBase    *pinterruptBase = pinterruptNodePvt->pinterruptBase;
    port             *pport = pinterruptBase->pport;
    
    epicsMutexMustLock(pport->asynManagerLock);
    if(pinterruptNodePvt->isOnList) {
        epicsMutexUnlock(pport->asynManagerLock);
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
            "freeInterruptNode requested but it is on a list");
        return asynError;
    }
    epicsMutexUnlock(pport->asynManagerLock);
    epicsMutexMustLock(pasynBase->lock);
    ellAdd(&pasynBase->interruptNodeFree,&pinterruptNode->node);
    epicsMutexUnlock(pasynBase->lock);
    return asynSuccess;
}

static asynStatus addInterruptUser(asynUser *pasynUser,
                                   interruptNode*pinterruptNode)
{
    interruptNodePvt *pinterruptNodePvt = interruptNodeToPvt(pinterruptNode);
    interruptBase    *pinterruptBase = pinterruptNodePvt->pinterruptBase;
    port             *pport = pinterruptBase->pport;
    
    epicsMutexMustLock(pport->asynManagerLock);
    if(pinterruptNodePvt->isOnList) {
        epicsMutexUnlock(pport->asynManagerLock);
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
            "asynManager:addInterruptUser already on list");
        return asynError;
    }
    while(pinterruptBase->callbackActive) {
        if(pinterruptNodePvt->isOnAddRemoveList) {
            epicsMutexUnlock(pport->asynManagerLock);
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                "asynManager:addInterruptUser already on addRemove list");
            return asynError;
        }
        ellAdd(&pinterruptBase->addRemoveList,&pinterruptNodePvt->addRemoveNode);
        pinterruptNodePvt->isOnAddRemoveList = TRUE;
        pinterruptBase->listModified = TRUE;
        epicsMutexUnlock(pport->asynManagerLock);
        epicsEventMustWait(pinterruptNodePvt->callbackDone);
        epicsMutexMustLock(pport->asynManagerLock);
    }
    ellAdd(&pinterruptBase->callbackList,&pinterruptNode->node);
    pinterruptNodePvt->isOnList = TRUE;
    epicsMutexUnlock(pport->asynManagerLock);
    return asynSuccess;
}

static asynStatus removeInterruptUser(asynUser *pasynUser,
                                   interruptNode*pinterruptNode)
{
    interruptNodePvt *pinterruptNodePvt = interruptNodeToPvt(pinterruptNode);
    interruptBase    *pinterruptBase = pinterruptNodePvt->pinterruptBase;
    port             *pport = pinterruptBase->pport;
    
    epicsMutexMustLock(pport->asynManagerLock);
    if(!pinterruptNodePvt->isOnList) {
        epicsMutexUnlock(pport->asynManagerLock);
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
            "asynManager:removeInterruptUser not on list");
        return asynError;
    }
    while(pinterruptBase->callbackActive) {
        if(pinterruptNodePvt->isOnAddRemoveList) {
            epicsMutexUnlock(pport->asynManagerLock);
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                "asynManager:removeInterruptUser already on addRemove list");
            return asynError;
        }
        ellAdd(&pinterruptBase->addRemoveList,&pinterruptNodePvt->addRemoveNode);
        pinterruptNodePvt->isOnAddRemoveList = TRUE;
        pinterruptBase->listModified = TRUE;
        epicsMutexUnlock(pport->asynManagerLock);
        epicsEventMustWait(pinterruptNodePvt->callbackDone);
        epicsMutexMustLock(pport->asynManagerLock);
    }
    ellDelete(&pinterruptBase->callbackList,&pinterruptNode->node);
    pinterruptNodePvt->isOnList = FALSE;
    epicsMutexUnlock(pport->asynManagerLock);
    return asynSuccess;
}

static asynStatus interruptStart(void *pasynPvt,ELLLIST **plist)
{
    interruptBase  *pinterruptBase = (interruptBase *)pasynPvt;
    port *pport = pinterruptBase->pport;

    epicsMutexMustLock(pport->asynManagerLock);
    pinterruptBase->callbackActive = TRUE;
    pinterruptBase->listModified = FALSE;
    epicsMutexUnlock(pport->asynManagerLock);
    *plist = (&pinterruptBase->callbackList);
    return asynSuccess;
}

static asynStatus interruptEnd(void *pasynPvt)
{
    interruptBase  *pinterruptBase = (interruptBase *)pasynPvt;
    port *pport = pinterruptBase->pport;
    interruptNodePvt *pinterruptNodePvt;

    epicsMutexMustLock(pport->asynManagerLock);
    pinterruptBase->callbackActive = FALSE;
    if(!pinterruptBase->listModified) {
        epicsMutexUnlock(pport->asynManagerLock);
        return asynSuccess;
    }
    while((pinterruptNodePvt = (interruptNodePvt *)ellFirst(
    &pinterruptBase->addRemoveList))){

        ellDelete(&pinterruptBase->addRemoveList,
            &pinterruptNodePvt->addRemoveNode);
        pinterruptNodePvt->isOnAddRemoveList = FALSE;
        epicsEventSignal(pinterruptNodePvt->callbackDone);
    }
    epicsMutexUnlock(pport->asynManagerLock);
    return asynSuccess;
}

/* Time stamp functions */

static void defaultTimeStampSource(void *userPvt, epicsTimeStamp *pTimeStamp)
{
    epicsTimeGetCurrent(pTimeStamp);
}


static asynStatus registerTimeStampSource(asynUser *pasynUser, void *pPvt, timeStampCallback callback) 
{
    userPvt    *puserPvt = asynUserToUserPvt(pasynUser);
    port *pport = puserPvt->pport;

    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:getAddr not connected to device");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    pport->timeStampSource = callback;
    pport->timeStampPvt = pPvt;
    epicsMutexUnlock(pport->asynManagerLock);
    return asynSuccess;
}

static asynStatus unregisterTimeStampSource(asynUser *pasynUser) 
{
    userPvt    *puserPvt = asynUserToUserPvt(pasynUser);
    port *pport = puserPvt->pport;

    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:getAddr not connected to device");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    pport->timeStampSource = defaultTimeStampSource;
    pport->timeStampPvt = 0;
    epicsMutexUnlock(pport->asynManagerLock);
    return asynSuccess;
}

static asynStatus updateTimeStamp(asynUser *pasynUser) 
{
    userPvt    *puserPvt = asynUserToUserPvt(pasynUser);
    port *pport = puserPvt->pport;
    asynStatus status=asynSuccess;

    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:updateTimeStamp not connected to device");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    if (pport->timeStampSource) {
        pport->timeStampSource(pport->timeStampPvt, &pport->timeStamp);
    } else {
        status = asynError;
    }
    epicsMutexUnlock(pport->asynManagerLock);
    return status;
}

static asynStatus getTimeStamp(asynUser *pasynUser, epicsTimeStamp *pTimeStamp) 
{
    userPvt    *puserPvt = asynUserToUserPvt(pasynUser);
    port *pport = puserPvt->pport;

    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:getTimeStamp not connected to device");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    pTimeStamp->secPastEpoch = pport->timeStamp.secPastEpoch;
    pTimeStamp->nsec = pport->timeStamp.nsec;
    epicsMutexUnlock(pport->asynManagerLock);
    return asynSuccess;
}

static asynStatus setTimeStamp(asynUser *pasynUser, const epicsTimeStamp *pTimeStamp) 
{
    userPvt    *puserPvt = asynUserToUserPvt(pasynUser);
    port *pport = puserPvt->pport;

    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:setTimeStamp not connected to device");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    pport->timeStamp.secPastEpoch = pTimeStamp->secPastEpoch;
    pport->timeStamp.nsec = pTimeStamp->nsec;
    epicsMutexUnlock(pport->asynManagerLock);
    return asynSuccess;
}



static asynStatus traceLock(asynUser *pasynUser)
{
    if(!pasynBase) asynInit();
    epicsMutexMustLock(pasynBase->lockTrace);
    return asynSuccess;
}

static asynStatus traceUnlock(asynUser *pasynUser)
{
    if(!pasynBase) asynInit();
    epicsMutexUnlock(pasynBase->lockTrace);
    return asynSuccess;
}

static asynStatus setTraceMask(asynUser *pasynUser,int mask)
{
    if(!pasynBase) asynInit();
    if(pasynUser == NULL) {
        pasynBase->trace.traceMask = mask;
    }
    else {
        userPvt *puserPvt = asynUserToUserPvt(pasynUser);
        port    *pport = puserPvt->pport;
        device  *pdevice = puserPvt->pdevice;

        if(!pport) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:setTraceMask -- not connected to port.");
            return asynError;
        }
        if(pdevice) {
            pdevice->dpc.trace.traceMask = mask;
            announceExceptionOccurred(pport, pdevice, asynExceptionTraceMask);
        }
        else {
            pdevice = (device *)ellFirst(&pport->deviceList);
            while(pdevice) {
                pdevice->dpc.trace.traceMask = mask;
                announceExceptionOccurred(pport, pdevice, asynExceptionTraceMask);
                pdevice = (device *)ellNext(&pdevice->node);
            }
            pport->dpc.trace.traceMask = mask;
            announceExceptionOccurred(pport, NULL, asynExceptionTraceMask);
        }
    }
    return asynSuccess;
}

static int getTraceMask(asynUser *pasynUser)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    tracePvt *ptracePvt  = findTracePvt(puserPvt);

    return ptracePvt->traceMask;
}

static asynStatus setTraceIOMask(asynUser *pasynUser,int mask)
{
    if(!pasynBase) asynInit();
    if(pasynUser == NULL) {
        pasynBase->trace.traceIOMask = mask;
    }
    else {
        userPvt *puserPvt = asynUserToUserPvt(pasynUser);
        port    *pport = puserPvt->pport;
        device  *pdevice = puserPvt->pdevice;

        if(!pport) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:setTraceIOMask -- not connected to port.");
            return asynError;
        }
        if(pdevice) {
            pdevice->dpc.trace.traceIOMask = mask;
            announceExceptionOccurred(pport, pdevice, asynExceptionTraceIOMask);
        }
        else {
            pdevice = (device *)ellFirst(&pport->deviceList);
            while(pdevice) {
                pdevice->dpc.trace.traceIOMask = mask;
                announceExceptionOccurred(pport, pdevice, asynExceptionTraceIOMask);
                pdevice = (device *)ellNext(&pdevice->node);
            }
            pport->dpc.trace.traceIOMask = mask;
            announceExceptionOccurred(pport, NULL, asynExceptionTraceIOMask);
        }
    }
    return asynSuccess;
}

static int getTraceIOMask(asynUser *pasynUser)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    tracePvt *ptracePvt  = findTracePvt(puserPvt);

    return ptracePvt->traceIOMask;
}

static asynStatus setTraceInfoMask(asynUser *pasynUser,int mask)
{
    if(!pasynBase) asynInit();
    if(pasynUser == NULL) {
        pasynBase->trace.traceInfoMask = mask;
    }
    else {
        userPvt *puserPvt = asynUserToUserPvt(pasynUser);
        port    *pport = puserPvt->pport;
        device  *pdevice = puserPvt->pdevice;

        if(!pport) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:setTraceInfoMask -- not connected to port.");
            return asynError;
        }
        if(pdevice) {
            pdevice->dpc.trace.traceInfoMask = mask;
            announceExceptionOccurred(pport, pdevice, asynExceptionTraceInfoMask);
        }
        else {
            pdevice = (device *)ellFirst(&pport->deviceList);
            while(pdevice) {
                pdevice->dpc.trace.traceInfoMask = mask;
                announceExceptionOccurred(pport, pdevice, asynExceptionTraceInfoMask);
                pdevice = (device *)ellNext(&pdevice->node);
            }
            pport->dpc.trace.traceInfoMask = mask;
            announceExceptionOccurred(pport, NULL, asynExceptionTraceInfoMask);
        }
    }
    return asynSuccess;
}

static int getTraceInfoMask(asynUser *pasynUser)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    tracePvt *ptracePvt  = findTracePvt(puserPvt);

    return ptracePvt->traceInfoMask;
}

static asynStatus setTraceFile(asynUser *pasynUser,FILE *fp)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    tracePvt *ptracePvt  = findTracePvt(puserPvt);

    epicsMutexMustLock(pasynBase->lockTrace);
    if(ptracePvt->type==traceFileFP) {
        int status;

        errno = 0;
        status = fclose(ptracePvt->fp);
        if(status) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:setTraceFile fclose error %s",strerror(errno));
        }
    }
    if(fp==0) {
        ptracePvt->type = traceFileErrlog; ptracePvt->fp = 0;
    } else if(fp==stdout) {
        ptracePvt->type = traceFileStdout; ptracePvt->fp = 0;
    } else if(fp==stderr) {
        ptracePvt->type = traceFileStderr; ptracePvt->fp = 0;
    } else {
        ptracePvt->type = traceFileFP; ptracePvt->fp = fp;
    }
    if(puserPvt->pport) exceptionOccurred(pasynUser,asynExceptionTraceFile);
    epicsMutexUnlock(pasynBase->lockTrace);
    return asynSuccess;
}

static FILE *getTraceFile(asynUser *pasynUser)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    tracePvt *ptracePvt  = findTracePvt(puserPvt);
    FILE     *fp = 0;

    switch(ptracePvt->type) {
        case traceFileErrlog: fp = 0;             break;
        case traceFileStdout: fp = stdout;        break;
        case traceFileStderr: fp = stderr;        break;
        case traceFileFP:     fp = ptracePvt->fp; break;
    }
    return fp;
}

static asynStatus setTraceIOTruncateSize(asynUser *pasynUser,size_t size)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    tracePvt *ptracePvt  = findTracePvt(puserPvt);

    epicsMutexMustLock(pasynBase->lockTrace);
    if(size>ptracePvt->traceBufferSize) {
        free(ptracePvt->traceBuffer);
        ptracePvt->traceBuffer = callocMustSucceed(size,sizeof(char),
            "asynTrace:setTraceIOTruncateSize");
        ptracePvt->traceBufferSize = size;
    }
    ptracePvt->traceTruncateSize = size;
    if(puserPvt->pport) exceptionOccurred(pasynUser,asynExceptionTraceIOTruncateSize);
    epicsMutexUnlock(pasynBase->lockTrace);
    return asynSuccess;
}

static size_t getTraceIOTruncateSize(asynUser *pasynUser)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    tracePvt *ptracePvt  = findTracePvt(puserPvt);

    return ptracePvt->traceTruncateSize;
}

static size_t printThread(FILE *fp)
{
    size_t nout = 0;
    unsigned int threadPriority = epicsThreadGetPrioritySelf();
    epicsThreadId threadId = epicsThreadGetIdSelf();
    if(fp) {
        nout = fprintf(fp,"[%s,%p,%d] ",epicsThreadGetNameSelf(),
                       (void*)threadId,threadPriority);
    } else {
        nout = errlogPrintf("[%s,%p,%d] ",epicsThreadGetNameSelf(),
                            (void*)threadId,threadPriority);
    }
    return nout;
}

static size_t printTime(FILE *fp)
{
    epicsTimeStamp now;
    char nowText[40];
    size_t rtn;

    rtn = epicsTimeGetCurrent(&now);
    if(rtn) {
        printf("epicsTimeGetCurrent failed\n");
        return 0;
    }
    nowText[0] = 0;
    epicsTimeToStrftime(nowText,sizeof(nowText),
         "%Y/%m/%d %H:%M:%S.%03f",&now);
    if(fp) {
        return fprintf(fp,"%s ",nowText);
    } else {
        return errlogPrintf("%s ",nowText);
    }
}

static size_t printPort(FILE *fp, asynUser *pasynUser)
{
    userPvt *puserPvt = asynUserToUserPvt(pasynUser);
    port    *pport = puserPvt->pport;
    int addr;
    size_t nout = 0;
    
    if(!pport) {
        return nout;
    }
    
    getAddr(pasynUser, &addr);
    if(fp) {
        nout = fprintf(fp,"[%s,%d,%d] ",pport->portName, addr, pasynUser->reason);
    } else {
        nout = errlogPrintf("[%s,%d,%d] ",pport->portName, addr, pasynUser->reason);
    }
    return nout;
}

static size_t printSource(FILE *fp, const char *file, int line)
{
    int      nout = 0;

    if(fp) {
        nout = fprintf(fp,"[%s:%d] ", file, line);
    } else {
        nout = errlogPrintf("[%s:%d] ", file, line);
    }
    return nout;
}

static int tracePrint(asynUser *pasynUser,int reason, const char *pformat, ...)
{
    va_list  pvar;
    int      nout = 0;

    va_start(pvar,pformat);
    nout = traceVprint(pasynUser, reason, pformat, pvar);
    va_end(pvar);
    return nout;
}

static int tracePrintSource(asynUser *pasynUser,int reason, const char *file, int line, const char *pformat, ...)
{
    va_list  pvar;
    int      nout = 0;

    va_start(pvar,pformat);
    nout = traceVprintSource(pasynUser, reason, file, line, pformat, pvar);
    va_end(pvar);
    return nout;
}

static int traceVprint(asynUser *pasynUser,int reason, const char *pformat, va_list pvar)
{
    return traceVprintSource(pasynUser, reason, "", 0, pformat, pvar);
}

static int traceVprintSource(asynUser *pasynUser,int reason, const char *file, int line, const char *pformat, va_list pvar)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    tracePvt *ptracePvt  = findTracePvt(puserPvt);
    int      nout = 0;
    FILE     *fp;

    if(!(reason & ptracePvt->traceMask)) return 0;
    epicsMutexMustLock(pasynBase->lockTrace);
    fp = getTraceFile(pasynUser);
    if (ptracePvt->traceInfoMask & ASYN_TRACEINFO_TIME) nout += (int)printTime(fp);
    if (ptracePvt->traceInfoMask & ASYN_TRACEINFO_PORT) nout += (int)printPort(fp, pasynUser);
    if (ptracePvt->traceInfoMask & ASYN_TRACEINFO_SOURCE) nout += (int)printSource(fp, file, line);
    if (ptracePvt->traceInfoMask & ASYN_TRACEINFO_THREAD) nout += (int)printThread(fp);
    if(fp) {
        nout += vfprintf(fp,pformat,pvar);
    } else {
        nout += errlogVprintf(pformat,pvar);
    }
    fflush(fp);
    epicsMutexUnlock(pasynBase->lockTrace);
    return nout;
}

static int tracePrintIO(asynUser *pasynUser,int reason,
    const char *buffer, size_t len,const char *pformat, ...)
{
    va_list  pvar;
    int      nout = 0;

    va_start(pvar,pformat);
    nout = traceVprintIO(pasynUser, reason, buffer, len, pformat, pvar);
    va_end(pvar);
    return nout;
}

static int tracePrintIOSource(asynUser *pasynUser,int reason,
    const char *buffer, size_t len, const char *file, int line, const char *pformat, ...)
{
    va_list  pvar;
    int      nout = 0;

    va_start(pvar,pformat);
    nout = traceVprintIOSource(pasynUser, reason, buffer, len, file, line, pformat, pvar);
    va_end(pvar);
    return nout;
}

static int traceVprintIO(asynUser *pasynUser,int reason,
    const char *buffer, size_t len,const char *pformat, va_list pvar)
{
    return traceVprintIOSource(pasynUser, reason, buffer, len, "", 0, pformat, pvar);
}

static int traceVprintIOSource(asynUser *pasynUser,int reason,
    const char *buffer, size_t len, const char *file, int line, const char *pformat, va_list pvar)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    tracePvt *ptracePvt  = findTracePvt(puserPvt);
    int      nout = 0;
    FILE     *fp;
    int traceMask,traceIOMask;
    size_t traceTruncateSize,nBytes;

    traceMask = ptracePvt->traceMask;
    traceIOMask = ptracePvt->traceIOMask;
    traceTruncateSize = ptracePvt->traceTruncateSize;
    if(!(reason&traceMask)) return 0;
    epicsMutexMustLock(pasynBase->lockTrace);
    fp = getTraceFile(pasynUser);
    if (ptracePvt->traceInfoMask & ASYN_TRACEINFO_TIME) nout += (int)printTime(fp);
    if (ptracePvt->traceInfoMask & ASYN_TRACEINFO_PORT) nout += (int)printPort(fp, pasynUser);
    if (ptracePvt->traceInfoMask & ASYN_TRACEINFO_SOURCE) nout += (int)printSource(fp, file, line);
    if (ptracePvt->traceInfoMask & ASYN_TRACEINFO_THREAD) nout += (int)printThread(fp);
    if(fp) {
        nout += vfprintf(fp,pformat,pvar);
    } else {
        nout += errlogVprintf(pformat,pvar);
    }
    nBytes = (len<traceTruncateSize) ? len : traceTruncateSize;
    if((traceIOMask&ASYN_TRACEIO_ASCII) && (nBytes>0)) {
       if(fp) {
           nout += fprintf(fp,"%.*s\n",(int)nBytes,buffer);
       } else {
           nout += errlogPrintf("%.*s\n",(int)nBytes,buffer);
       }
    }
    if(traceIOMask&ASYN_TRACEIO_ESCAPE) {
        if(nBytes>0) {
            if(fp) {
                nout += epicsStrPrintEscaped(fp,buffer,nBytes);
                nout += fprintf(fp,"\n");
            } else {
                nout += epicsStrSnPrintEscaped(ptracePvt->traceBuffer,
                                               ptracePvt->traceBufferSize,
                                               buffer,
                                               nBytes);
                errlogPrintf("%s\n",ptracePvt->traceBuffer);
            }
        }
    }
    if((traceIOMask&ASYN_TRACEIO_HEX) && (traceTruncateSize>0)) {
        size_t i;
        for(i=0; i<nBytes; i++) {
            if(i%20 == 0) {
                if(fp) {
                    nout += fprintf(fp,"\n");
                } else {
                    nout += errlogPrintf("\n");
                }
            }
            if(fp) {
                nout += fprintf(fp,"%2.2x ",(unsigned char)buffer[i]);
            } else {
                nout += errlogPrintf("%2.2x ",(unsigned char)buffer[i]);
            }
        }
        if(fp) {
            nout += fprintf(fp,"\n");
        } else {
            nout += errlogPrintf("\n");
        }
    }
    /* If the traceIOMask is 0 or traceTruncateSize <=0 we need to output a newline */
    if((traceIOMask == 0) || (traceTruncateSize <=0)) {
        if(fp) {
            nout += fprintf(fp,"\n");
        } else {
            nout += errlogPrintf("\n");
        }
    }
    fflush(fp);
    epicsMutexUnlock(pasynBase->lockTrace);
    return nout;
}

/*
 * User-readable status code
 */
static const char *strStatus(asynStatus status)
{
    switch (status) {
    case asynSuccess:   return "asynSuccess";
    case asynTimeout:   return "asynTimeout";
    case asynOverflow:  return "asynOverflow";
    case asynError:     return "asynError";
    default:            return "asyn????";
    }
}

/*
 * functions for portConnect 
 */
static void initPortConnect(port *pport)
{
    asynStatus status;
    asynUser *pasynUser;
    int addr = -1;
    asynInterface *pasynInterface;
    pasynUser = pasynManager->createAsynUser(
       portConnectProcessCallback,0);
    pport->pconnectUser = pasynUser;
    status = pasynManager->connectDevice(pasynUser,pport->portName,addr);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s %d autoConnect connectDevice failed.\n",
            pport->portName,addr);
        return;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,FALSE);
    if(pasynInterface==0) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
           "%s %d autoConnect findInterface failed.\n",
         pport->portName,addr);
         return;
    }
    pport->pcommonInterface = pasynInterface;
    pport->connectTimer = epicsTimerQueueCreateTimer(
        pasynBase->connectPortTimerQueue,
        portConnectTimerCallback, pport);
    pport->secondsBetweenPortConnect = DEFAULT_SECONDS_BETWEEN_PORT_CONNECT;
}

static void portConnectTimerCallback(void *pvt)
{
    port *pport = (port *)pvt;
    asynUser *pasynUser = pport->pconnectUser;
    asynStatus status;
    int addr = -1;
    if(!pport->dpc.connected && pport->dpc.autoConnect) {
        status = pasynManager->queueRequest(pasynUser, asynQueuePriorityConnect, 0);
        if(status != asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
               "%s %d queueRequest failed.\n",
             pport->portName,addr);
        }
    }
}

static void portConnectProcessCallback(asynUser *pasynUser)
{
    asynStatus status = asynSuccess;
    userPvt *puserPvt = asynUserToUserPvt(pasynUser);
    port    *pport = puserPvt->pport;
    asynInterface *pasynInterface = pport->pcommonInterface;
    asynCommon *pasynCommon = (asynCommon *)pasynInterface->pinterface;
    void *drvPvt = pasynInterface->drvPvt;
    int isConnected;
    /* We were not connected when the request was queued, but we could have connected since then */
    status = pasynManager->isConnected(pasynUser, &isConnected);
    if (!isConnected) status = pasynCommon->connect(drvPvt,pasynUser);
    if(status!=asynSuccess) {
        epicsTimerStartDelay(pport->connectTimer,pport->secondsBetweenPortConnect);
    }
}
static void waitConnectExceptionHandler(asynUser *pasynUser, asynException exception)
{
    epicsEventId connectEvent = (epicsEventId)pasynUser->userPvt;

    if (exception == asynExceptionConnect)
        epicsEventSignal(connectEvent);
}

static asynStatus waitConnect(asynUser *pasynUser, double timeout)
{
    userPvt       *puserPvt = asynUserToUserPvt(pasynUser);
    port          *pport = puserPvt->pport;
    dpCommon      *pdpCommon = findDpCommon(puserPvt);
    epicsEventId  connectEvent;
    asynUser      *pasynUserCopy;
    int           isConnected;
    asynStatus    status;
    epicsEventWaitStatus waitStatus;

    if(!pdpCommon) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "asynManager:waitConnect asynUser not connected to device\n");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    isConnected = pport->dpc.connected;
    epicsMutexUnlock(pport->asynManagerLock);
    if (isConnected) return(asynSuccess);

    connectEvent = epicsEventMustCreate(epicsEventEmpty);
    pasynUserCopy = pasynManager->duplicateAsynUser(pasynUser, 0, 0);
    pasynUserCopy->userPvt = connectEvent;
    status = pasynManager->exceptionCallbackAdd(pasynUserCopy, waitConnectExceptionHandler);
    if (status) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR, 
            "asynManager:waitConnect port=%s error calling exceptionCallbackAdd\n",
            pport->portName);
        return asynError;
   }
    asynPrint(pasynUser, ASYN_TRACE_FLOW, 
        "asynManager:waitConnect port=%s waiting for connection event\n",
        pport->portName);
    waitStatus = epicsEventWaitWithTimeout(connectEvent, timeout);
    isConnected = (waitStatus == epicsEventWaitOK);

    pasynManager->exceptionCallbackRemove(pasynUserCopy);
    epicsEventDestroy(connectEvent);
    asynPrint(pasynUser, ASYN_TRACE_FLOW, 
        "asynManager:waitConnect port=%s exit, isConnected=%d\n", 
        pport->portName, isConnected);
    pasynManager->freeAsynUser(pasynUserCopy);
    if (isConnected) return asynSuccess; 
    return asynError;
}
