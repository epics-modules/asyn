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

#define epicsExportSharedSymbols
#include "asynDriver.h"

#define BOOL int
#define TRUE 1
#define FALSE 0
#define ERROR_MESSAGE_SIZE 160
#define NUMBER_QUEUE_PRIORITIES (asynQueuePriorityConnect + 1)
#define DEFAULT_TRACE_TRUNCATE_SIZE 80
#define DEFAULT_TRACE_BUFFER_SIZE 80

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
    traceFileType type;
    FILE          *fp;
    int           traceTruncateSize;
    int           traceBufferSize;
    char          *traceBuffer;
};

#define nMemList 9
static int memListSize[nMemList] =
    {16,32,64,128,256,512,1024,2048,4096};
typedef struct memNode {
    ELLNODE node;
    void    *memory;
}memNode;

typedef struct asynBase {
    ELLLIST           asynPortList;
    ELLLIST           asynUserFreeList;
    ELLLIST           interruptNodeFree;
    epicsTimerQueueId timerQueue;
    epicsMutexId      lock;
    epicsMutexId      lockTrace;
    tracePvt          trace;
    ELLLIST           memList[nMemList];
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
    epicsMutexId   syncLock; /* lock/unlock for synchronousRequest*/
    BOOL           enabled;
    BOOL           connected;
    BOOL           autoConnect;
    userPvt        *plockHolder;
    ELLLIST        interposeInterfaceList;
    ELLLIST        exceptionUserList;
    ELLLIST        exceptionNotifyList;
    BOOL           exceptionActive;
    epicsTimeStamp lastConnectStateChange;
    unsigned long  numberConnects;
    tracePvt       trace;
    port           *pport;
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
    BOOL          freeAfterCallback;
    exceptionUser *pexceptionUser;
    BOOL          isQueued;
    /* state,...,timeout are for queueRequest callbacks*/
    callbackState state;
    epicsEventId  callbackDone;
    userCallback  processUser;
    userCallback  timeoutUser;
    epicsTimerId  timer;
    double        timeout;
    unsigned int  lockCount;
    port          *pport;
    device        *pdevice;
    asynUser      user;
};

struct device {
    ELLNODE   node;     /*For asynPort.deviceList*/
    dpCommon  dpc;
    int       addr;
};

struct port {
    ELLNODE       node;  /*For asynBase.asynPortList*/
    char          *portName;
    epicsMutexId  asynManagerLock; /*for asynManager*/
    epicsMutexId  synchronousLock; /*for synchronous drivers*/
    dpCommon      dpc;
    asynUser      *pasynUser; /*For autoConnect*/
    ELLLIST       deviceList;
    ELLLIST       interfaceList;
    int           attributes;
    /*The following are only initialized/used if attributes&ASYN_CANBLOCK*/
    ELLLIST       queueList[NUMBER_QUEUE_PRIORITIES];
    BOOL          queueStateChange;
    epicsEventId  notifyPortThread;
    epicsThreadId threadid;
};

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
static void asynInit(void);
static void dpCommonInit(port *pport,dpCommon *pdpCommon,BOOL autoConnect);
static dpCommon *findDpCommon(userPvt *puserPvt);
static tracePvt *findTracePvt(userPvt *puserPvt);
static port *locatePort(const char *portName);
static device *locateDevice(port *pport,int addr,BOOL allocNew);
static interfaceNode *locateInterfaceNode(
            ELLLIST *plist,const char *interfaceType,BOOL allocNew);
static void exceptionOccurred(asynUser *pasynUser,asynException exception);
static void queueTimeoutCallback(void *pvt);
static void autoConnect(port *pport,int addr);
static BOOL isPortEnabled(userPvt *puserPvt);
static BOOL isPortUp(userPvt *puserPvt);
static BOOL isDeviceEnabled(userPvt *puserPvt,dpCommon *pdpCommon);
static BOOL isDeviceUp(userPvt *puserPvt,dpCommon *pdpCommon);
static void portThread(port *pport);
    
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
static asynStatus canBlock(asynUser *pasynUser,int *yesNo);
static asynStatus lock(asynUser *pasynUser);
static asynStatus unlock(asynUser *pasynUser);
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
    canBlock,
    lock,
    unlock,
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
    registerInterruptSource,
    getInterruptPvt,
    createInterruptNode,
    freeInterruptNode,
    addInterruptUser,
    removeInterruptUser,
    interruptStart,
    interruptEnd
};
epicsShareDef asynManager *pasynManager = &manager;

/* asynTrace methods */
static asynStatus traceLock(asynUser *pasynUser);
static asynStatus traceUnlock(asynUser *pasynUser);
static asynStatus setTraceMask(asynUser *pasynUser,int mask);
static int        getTraceMask(asynUser *pasynUser);
static asynStatus setTraceIOMask(asynUser *pasynUser,int mask);
static int        getTraceIOMask(asynUser *pasynUser);
static asynStatus setTraceFile(asynUser *pasynUser,FILE *fp);
static FILE       *getTraceFile(asynUser *pasynUser);
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
    setTraceFile,
    getTraceFile,
    setTraceIOTruncateSize,
    getTraceIOTruncateSize,
    tracePrint,
    tracePrintIO
};
epicsShareDef asynTrace *pasynTrace = &asynTraceManager;

/*internal methods */
static void tracePvtInit(tracePvt *ptracePvt)
{
    ptracePvt->traceBuffer = callocMustSucceed(
        DEFAULT_TRACE_BUFFER_SIZE,sizeof(char),
        "asynManager:tracePvtInit");
    ptracePvt->traceMask = ASYN_TRACE_ERROR;
    ptracePvt->traceTruncateSize = DEFAULT_TRACE_TRUNCATE_SIZE;
    ptracePvt->traceBufferSize = DEFAULT_TRACE_BUFFER_SIZE;
    ptracePvt->type = traceFileStdout;
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
}

static void dpCommonInit(port *pport,dpCommon *pdpCommon,BOOL autoConnect)
{
    pdpCommon->syncLock = epicsMutexMustCreate();
    pdpCommon->enabled = TRUE;
    pdpCommon->connected = FALSE;
    pdpCommon->autoConnect = autoConnect;
    ellInit(&pdpCommon->interposeInterfaceList);
    ellInit(&pdpCommon->exceptionUserList);
    ellInit(&pdpCommon->exceptionNotifyList);
    pdpCommon->pport = pport;
    tracePvtInit(&pdpCommon->trace);
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
        dpCommonInit(pport,&pdevice->dpc,pport->dpc.autoConnect);
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
static void exceptionOccurred(asynUser *pasynUser,asynException exception)
{
    userPvt    *puserPvt = asynUserToUserPvt(pasynUser);
    port       *pport = puserPvt->pport;
    device     *pdevice = puserPvt->pdevice;
    int        addr = (pdevice ? pdevice->addr : -1);
    dpCommon   *pdpCommon = findDpCommon(puserPvt);
    exceptionUser *pexceptionUser;

    assert(pport&&pdpCommon);
    epicsMutexMustLock(pport->asynManagerLock);
    pdpCommon->exceptionActive = TRUE;
    epicsMutexUnlock(pport->asynManagerLock);
    pexceptionUser = (exceptionUser *)ellFirst(&pdpCommon->exceptionUserList);
    while(pexceptionUser) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s %d exceptionOccurred calling exceptionUser\n",
            pport->portName,addr, (int)exception);
        pexceptionUser->callback(pexceptionUser->pasynUser,exception);
        pexceptionUser = (exceptionUser *)ellNext(&pexceptionUser->node);
    }
    epicsMutexMustLock(pport->asynManagerLock);
    while((pexceptionUser  =
    (exceptionUser *)ellFirst(&pdpCommon->exceptionNotifyList))) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s %d exceptionOccurred notify\n",
            pport->portName,addr, (int)exception);
        epicsEventSignal(pexceptionUser->notify);
        ellDelete(&pdpCommon->exceptionNotifyList,&pexceptionUser->notifyNode);
    }
    pdpCommon->exceptionActive = FALSE;
    pport->queueStateChange = TRUE;
    epicsMutexUnlock(pport->asynManagerLock);
    if(pport->attributes&ASYN_CANBLOCK)
        epicsEventSignal(pport->notifyPortThread);
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
    }
    epicsMutexUnlock(pport->asynManagerLock);
    epicsEventSignal(pport->notifyPortThread);
}

static void autoConnect(port *pport,int addr)
{
    asynUser       *pasynUser = pport->pasynUser;
    userPvt        *puserPvt = asynUserToUserPvt(pasynUser);
    dpCommon       *pdpCommon;
    epicsTimeStamp now;
    double         secsSinceDisconnect;
    asynInterface  *pasynInterface;
    asynCommon     *pasynCommon = 0;
    void           *drvPvt = 0;
    asynStatus     status;

    status = pasynManager->connectDevice(pasynUser,pport->portName,addr);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s %d autoConnect connectDevice failed.\n",
            pport->portName,addr);
        return;
    }
    pdpCommon = findDpCommon(puserPvt);
    assert(pdpCommon);
    epicsTimeGetCurrent(&now);
    secsSinceDisconnect = epicsTimeDiffInSeconds(
        &now,&pdpCommon->lastConnectStateChange);
    if(secsSinceDisconnect<2.0) epicsThreadSleep(2.0 - secsSinceDisconnect);
    epicsTimeGetCurrent(&pdpCommon->lastConnectStateChange);
    pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,TRUE);
    if(!pasynInterface) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s %d autoConnect findInterface failed.\n",
            pport->portName,addr);
            goto disconnect;
    }
    pasynCommon = (asynCommon *)pasynInterface->pinterface;
    drvPvt = pasynInterface->drvPvt;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d autoConnect\n",pport->portName,addr);
    pasynUser->errorMessage[0] = '\0';
    status = pasynCommon->connect(drvPvt,pasynUser);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s %d autoConnect could not connect\n",
            pasynUser->errorMessage,pport->portName,addr);
    }
disconnect:
    status = pasynManager->disconnect(pasynUser);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s %d autoConnect disconnect failed.\n",
            pport->portName,addr);
    }
}

static BOOL isPortEnabled(userPvt *puserPvt)
{
    port     *pport = puserPvt->pport;

    assert(pport);
    if(!pport->dpc.enabled) {
        asynUser *pasynUser = userPvtToAsynUser(puserPvt);
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s port disabled\n",pport->portName);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager port disabled\n");
        return FALSE;
    }
    return TRUE;
}

static BOOL isPortUp(userPvt *puserPvt)
{
    asynUser *pasynUser = userPvtToAsynUser(puserPvt);
    port     *pport = puserPvt->pport;

    if(pport->dpc.connected) return TRUE;
    if(pport->dpc.autoConnect) autoConnect(pport,-1);
    if(!pport->dpc.connected) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s asynManager port not connected\n",pport->portName);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "port not connected\n");
        return FALSE;
    }
    return TRUE;
}

static BOOL isDeviceEnabled(userPvt *puserPvt,dpCommon *pdpCommon)
{
    asynUser *pasynUser = userPvtToAsynUser(puserPvt);
    assert(pdpCommon);
    if(pdpCommon!=&puserPvt->pport->dpc && !isPortEnabled(puserPvt))
        return FALSE;
    if(!pdpCommon->enabled) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s device disabled\n",puserPvt->pport->portName);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "device disabled\n");
        return FALSE;
    }
    return TRUE;
}

static BOOL isDeviceUp(userPvt *puserPvt,dpCommon *pdpCommon)
{
    asynUser *pasynUser = userPvtToAsynUser(puserPvt);
    port     *pport = puserPvt->pport;
    int      addr;
    asynStatus status;

    if(!isDeviceEnabled(puserPvt,pdpCommon)) return FALSE;
    if(pdpCommon!=&pport->dpc && !isPortUp(puserPvt)) return FALSE;
    if(pdpCommon->connected) return TRUE;
    status = getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return FALSE;
    if(pdpCommon->autoConnect) autoConnect(pport,addr);
    if(!pdpCommon->connected) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s %s device not connected\n",pport->portName,addr);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "not connected\n");
        return FALSE;
    }
    return TRUE;
}

static void portThread(port *pport)
{
    userPvt  *puserPvt;
    asynUser *pasynUser;
    double   timeout;

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
            puserPvt->processUser(pasynUser);
            epicsMutexMustLock(pport->asynManagerLock);
            if (puserPvt->state==callbackCanceled)
                epicsEventSignal(puserPvt->callbackDone);
            puserPvt->state = callbackIdle;
        }
        if(!pport->dpc.connected && pport->dpc.autoConnect) {
            epicsMutexUnlock(pport->asynManagerLock);
            autoConnect(pport,-1);
            epicsMutexMustLock(pport->asynManagerLock);
        }
        if(!pport->dpc.connected) {
            epicsMutexUnlock(pport->asynManagerLock);
            continue;
        }
        while(1) {
            int i;
            dpCommon *pdpCommon = 0;
            pport->queueStateChange = FALSE;
            for(i=asynQueuePriorityHigh; i>=asynQueuePriorityLow; i--) {
                puserPvt = (userPvt *)ellFirst(&pport->queueList[i]);
                while(puserPvt){
                    pdpCommon = findDpCommon(puserPvt);
                    assert(pdpCommon);
                    if(isDeviceUp(puserPvt,pdpCommon)) {
                        if(!pdpCommon->plockHolder
                        || pdpCommon->plockHolder==puserPvt) {
                            assert(puserPvt->isQueued);
                            ellDelete(&pport->queueList[i],&puserPvt->node);
                            puserPvt->isQueued = FALSE;
                            break; /*while(puserPvt)*/
                        }
                    }
                    puserPvt = (userPvt *)ellNext(&puserPvt->node);
                }
                if(puserPvt || pport->queueStateChange) break; /*for*/
            }
            if(!puserPvt) break; /*while(1)*/
            pasynUser = userPvtToAsynUser(puserPvt);
            if(puserPvt->lockCount>0) pdpCommon->plockHolder = puserPvt;
            pasynUser->errorMessage[0] = '\0';
            asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s callback\n",pport->portName);
            puserPvt->state = callbackActive;
            timeout = puserPvt->timeout;
            epicsMutexUnlock(pport->asynManagerLock);
            if(puserPvt->timer && timeout>0.0) epicsTimerCancel(puserPvt->timer);
            puserPvt->processUser(pasynUser);
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
            if(pport->queueStateChange) break;
        }
        epicsMutexUnlock(pport->asynManagerLock);
    }
}

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


static void reportPrintPort(port *pport, FILE *fp,int details)
{
    int           i;
    dpCommon      *pdpc;
    device        *pdevice;
    interfaceNode *pinterfaceNode;
    asynCommon    *pasynCommon = 0;
    void          *drvPvt = 0;
    int           nQueued = 0;
    int           lockCount;

    for(i=asynQueuePriorityLow; i<=asynQueuePriorityConnect; i++) 
        nQueued += ellCount(&pport->queueList[i]);
    pdpc = &pport->dpc;
    fprintf(fp,"%s multiDevice:%s canBlock:%s autoConnect:%s\n    "
            "enabled:%s connected:%s numberConnects %lu\n",
            pport->portName,
            ((pport->attributes&ASYN_MULTIDEVICE) ? "Yes" : "No"),
            ((pport->attributes&ASYN_CANBLOCK) ? "Yes" : "No"),
            (pdpc->autoConnect ? "Yes" : "No"),
            (pdpc->enabled ? "Yes" : "No"),
            (pdpc->connected ? "Yes" : "No"),
             pdpc->numberConnects);
    epicsMutexMustLock(pport->asynManagerLock);
    lockCount = (pdpc->plockHolder) ? pdpc->plockHolder->lockCount : 0;
    epicsMutexUnlock(pport->asynManagerLock);
    fprintf(fp,"    nDevices %d nQueued %d lockCount %d\n",
            ellCount(&pport->deviceList),nQueued,lockCount);
    fprintf(fp,"    exceptionActive: %s "
            "exceptionUsers %d exceptionNotifys %d\n",
            (pdpc->exceptionActive ? "Yes" : "No"),
            ellCount(&pdpc->exceptionUserList),
            ellCount(&pdpc->exceptionNotifyList));
    reportPrintInterfaceList(fp,&pdpc->interposeInterfaceList,
                             "interposeInterfaceList");
    reportPrintInterfaceList(fp,&pport->interfaceList,"interfaceList");
    pdevice = (device *)ellFirst(&pport->deviceList);
    while(pdevice) {
        pdpc = &pdevice->dpc;
        fprintf(fp,"    addr:%d",pdevice->addr);
        fprintf(fp," autoConnect:%s enabled:%s "
                "connected:%s exceptionActive:%s\n",
                (pdpc->autoConnect ? "Yes" : "No"),
                (pdpc->enabled ? "Yes" : "No"),
                (pdpc->connected ? "Yes" : "No"),
                (pdpc->exceptionActive ? "Yes" : "No"));
        epicsMutexMustLock(pport->asynManagerLock);
        lockCount = (pdpc->plockHolder) ? pdpc->plockHolder->lockCount : 0;
        epicsMutexUnlock(pport->asynManagerLock);
        fprintf(fp,"    exceptionActive: %s "
                "exceptionUsers %d exceptionNotifys %d lockCount %d\n",
                (pdpc->exceptionActive ? "Yes" : "No"),
                ellCount(&pdpc->exceptionUserList),
                ellCount(&pdpc->exceptionNotifyList),lockCount);
        reportPrintInterfaceList(fp,&pdpc->interposeInterfaceList,
                                 "interposeInterfaceList");
        pdevice = (device *)ellNext(&pdevice->node);
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
        fprintf(fp,"    Calling asynCommon.report\n");
        pasynCommon->report(drvPvt,fp,details);
    }
}

static void report(FILE *fp,int details,const char *portName)
{
    port *pport;

    if(!pasynBase) asynInit();
    if (portName) {
        pport = locatePort(portName);
        if(!pport) {
            fprintf(fp, "asynManager:report port %s not found\n",portName);
            return;
        }
        reportPrintPort(pport,fp,details);
    } else {
        pport = (port *)ellFirst(&pasynBase->asynPortList);
        while(pport) {
            reportPrintPort(pport,fp,details);
            pport = (port *)ellNext(&pport->node);
        }
    }
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
        pasynUser = userPvtToAsynUser(puserPvt);
        pasynUser->errorMessage = (char *)(puserPvt +1);
        pasynUser->errorMessageSize = ERROR_MESSAGE_SIZE;
        puserPvt->callbackDone = epicsEventMustCreate(epicsEventEmpty);
        puserPvt->timer = epicsTimerQueueCreateTimer(
            pasynBase->timerQueue,queueTimeoutCallback,puserPvt);
    } else {
        ellDelete(&pasynBase->asynUserFreeList,&puserPvt->node);
        epicsMutexUnlock(pasynBase->lock);
        pasynUser = userPvtToAsynUser(puserPvt);
    }
    puserPvt->processUser = process;
    puserPvt->timeoutUser = timeout;
    puserPvt->isQueued = FALSE;
    puserPvt->state = callbackIdle;
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
    pnew->user.userPvt = pold->user.userPvt;
    pnew->user.userData = pold->user.userData;
/*MARTY IS THIS SAFE*/
    pnew->user.drvUser = pold->user.drvUser;
    pnew->user.reason = pold->user.reason;
    pnew->user.timeout = pold->user.timeout;
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
        epicsMutexMustLock(pasynBase->lock);
        ellAdd(&pasynBase->asynUserFreeList,&puserPvt->node);
        epicsMutexUnlock(pasynBase->lock);
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
    
    for(ind=0; ind<nMemList; ind++) {
        if(size<=memListSize[ind]) break;
    }
    if(ind>=nMemList) {
        return mallocMustSucceed(size,"asynManager::memCalloc");
    }
    pmemList = &pasynBase->memList[ind];
    epicsMutexMustLock(pasynBase->lock);
    pmemNode = (memNode *)ellFirst(pmemList);
    if(pmemNode) {
        ellDelete(pmemList,&pmemNode->node);
    } else {
        pmemNode = callocMustSucceed(1,sizeof(memNode)+size,
            "asynManager::memCalloc");
        pmemNode->memory = pmemNode + 1;
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
    if(size>memListSize[nMemList-1]) {
        free(pmem);
        return;
    }
    for(ind=0; ind<nMemList; ind++) {
        if(size<=memListSize[ind]) break;
    }
    assert(ind<nMemList);
    pmemList = &pasynBase->memList[ind];
    pmemNode = pmem;
    pmemNode--;
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
                "asynManager:connectDevice port %s not found\n",portName);
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
                "asynManager:connectDevice port %s not found\n",portName);
        return asynError;
    }
    if(puserPvt->pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:connectDevice already connected to device\n");
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
                "asynManager::disconnect: not connected\n");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    if(puserPvt->isQueued) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::disconnect request queued\n");
        status = asynError; goto unlock;
    }
    if(puserPvt->lockCount>0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::disconnect: isLocked\n");
        status = asynError; goto unlock;
    }
    if(puserPvt->pexceptionUser) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::disconnect: on exceptionCallback list\n");
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
            "asynManager:exceptionCallbackAdd callback was NULL\n");
        return asynError;
    }
    if(!pport || !pdpCommon) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:exceptionCallbackAdd not connected\n");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    pexceptionUser = puserPvt->pexceptionUser;
    if(pexceptionUser) {
        epicsMutexUnlock(pport->asynManagerLock);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:exceptionCallbackAdd already on list\n");
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
            "asynManager:exceptionCallbackRemove not connected\n");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    pexceptionUser = puserPvt->pexceptionUser;
    if(!pexceptionUser) {
        epicsMutexUnlock(pport->asynManagerLock);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:exceptionCallbackRemove not on list\n");
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
                "asynManager:findInterface: not connected\n");
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

    assert(priority>=asynQueuePriorityLow && priority<=asynQueuePriorityConnect);
    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::queueRequest not connected\n");
        return asynError;
    }
    if(!(pport->attributes&ASYN_CANBLOCK)) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s queueRequest synchronousRequest\n",
            pport->portName);
        pasynUser->errorMessage[0] = '\0';
        if(!isDeviceEnabled(puserPvt,pdpCommon)) return asynError;
        if(priority!=asynQueuePriorityConnect && !isDeviceUp(puserPvt,pdpCommon))
            return asynError;
        epicsMutexMustLock(pdpCommon->syncLock);
        epicsMutexMustLock(pport->synchronousLock);
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s queueRequest calling callback\n",
            pport->portName);
        puserPvt->processUser(pasynUser);
        epicsMutexUnlock(pport->synchronousLock);
        epicsMutexUnlock(pdpCommon->syncLock);
        return asynSuccess;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    if(puserPvt->isQueued) {
        epicsMutexUnlock(pport->asynManagerLock);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::queueRequest is already queued\n");
        return asynError;
    }
    if(pdpCommon->plockHolder && pdpCommon->plockHolder==puserPvt) {
        /*Add to front of list*/
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
            "%s schedule queueRequest timeout\n",puserPvt->pport->portName);
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

static asynStatus lock(asynUser *pasynUser)
{
    userPvt *puserPvt = asynUserToUserPvt(pasynUser);
    port    *pport = puserPvt->pport;
    int     can;

    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::lock not connected\n");
        return asynError;
    }
    if(puserPvt->isQueued) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::lock is queued\n");
        return asynError;
    }
    canBlock(pasynUser,&can);
    if(!can) {
        dpCommon *pdpCommon = findDpCommon(puserPvt);

        assert(pdpCommon);
        epicsMutexMustLock(pdpCommon->syncLock);
    } else {
        epicsMutexMustLock(pport->asynManagerLock);
        puserPvt->lockCount++;
        epicsMutexUnlock(pport->asynManagerLock);
    }
    return asynSuccess;
}

static asynStatus unlock(asynUser *pasynUser)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    port     *pport = puserPvt->pport;
    dpCommon *pdpCommon = findDpCommon(puserPvt);
    BOOL     wasOwner = FALSE;
    int      can;

    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::unlock not connected\n");
        return asynError;
    }
    if(puserPvt->isQueued) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::unlock is queued\n");
        return asynError;
    }
    canBlock(pasynUser,&can);
    if(!can) {
        dpCommon *pdpCommon = findDpCommon(puserPvt);

        assert(pdpCommon);
        epicsMutexUnlock(pdpCommon->syncLock);
        return asynSuccess;
    }
    if(puserPvt->lockCount==0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::unlock but not locked\n");
        return asynError;
    }
    epicsMutexMustLock(pport->asynManagerLock);
    puserPvt->lockCount--;
    if(puserPvt->lockCount==0 && pdpCommon->plockHolder==puserPvt) {
        pdpCommon->plockHolder = 0;
        wasOwner = TRUE;
    }
    epicsMutexUnlock(pport->asynManagerLock);
    if(pport->attributes&ASYN_CANBLOCK && wasOwner)
        epicsEventSignal(pport->notifyPortThread);
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
    int     i,len;

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
    dpCommonInit(pport,&pport->dpc,autoConnect);
    pport->pasynUser = createAsynUser(0,0);
    ellInit(&pport->deviceList);
    ellInit(&pport->interfaceList);
    epicsMutexMustLock(pasynBase->lock);
    ellAdd(&pasynBase->asynPortList,&pport->node);
    epicsMutexUnlock(pasynBase->lock);
    if(!(attributes&ASYN_CANBLOCK)) return asynSuccess;
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
        return asynError;
    }
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
    return asynSuccess;
}

static asynStatus exceptionConnect(asynUser *pasynUser)
{
    userPvt    *puserPvt = asynUserToUserPvt(pasynUser);
    port       *pport = puserPvt->pport;
    dpCommon   *pdpCommon = findDpCommon(puserPvt);

    if(!pport || !pdpCommon) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:enable not connected to port/device\n");
        return asynError;
    }
    if(pdpCommon->connected) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s addr %d asynManager:exceptionConnect already connected\n",
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
            "asynManager:enable not connected\n");
        return asynError;
    }
    if(!pdpCommon->connected) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s addr %d asynManager:exceptionConnect but not connected\n",
            pport->portName, (puserPvt->pdevice ? puserPvt->pdevice->addr : -1));
        return asynError;
    }
    pdpCommon->connected = FALSE;
    epicsTimeGetCurrent(&pdpCommon->lastConnectStateChange);
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
            "asynManager:enable not connected\n");
        return asynError;
    }
    pdpCommon->enabled = yesNo&1;
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
            "asynManager:autoConnect not connected\n");
        return asynError;
    }
    pdpCommon->autoConnect = yesNo&1;
    exceptionOccurred(pasynUser,asynExceptionAutoConnect);
    return asynSuccess;
}

static asynStatus isConnected(asynUser *pasynUser,int *yesNo)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    dpCommon *pdpCommon = findDpCommon(puserPvt);

    if(!pdpCommon) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:isConnected asynUser not connected to device\n");
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
            "asynManager:isEnabled asynUser not connected to device\n");
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
            "asynManager:isAutoConnect asynUser not connected to device\n");
        return asynError;
    }
    *yesNo = pdpCommon->autoConnect;
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
            "no connected to a port\n");
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
            "interface %s is not registered\n",interfaceType);
        return asynError;
    }
    *pasynPvt = pinterfaceNode->pinterruptBase;
    epicsMutexUnlock(pport->asynManagerLock);
    if (!*pasynPvt) {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                      "Driver does not support interrupts on interface %s",
                      interfaceType);
        return(asynError);
    }
    return asynSuccess;
}

static interruptNode *createInterruptNode(void *pasynPvt)
{
    interruptBase    *pinterruptBase = (interruptBase *)pasynPvt;
    port             *pport = pinterruptBase->pport;
    interruptNode    *pinterruptNode;
    interruptNodePvt *pinterruptNodePvt;

    epicsMutexMustLock(pport->asynManagerLock);
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
    epicsMutexUnlock(pport->asynManagerLock);
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
            "freeInterruptNode requested but it is on a list\n");
        return asynError;
    }
    epicsMutexMustLock(pasynBase->lock);
    ellAdd(&pasynBase->interruptNodeFree,&pinterruptNode->node);
    epicsMutexUnlock(pasynBase->lock);
    epicsMutexUnlock(pport->asynManagerLock);
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
            "asynManager:addInterruptUser already on list\n");
        return asynError;
    }
    while(pinterruptBase->callbackActive) {
        if(pinterruptNodePvt->isOnAddRemoveList) {
            epicsMutexUnlock(pport->asynManagerLock);
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                "asynManager:addInterruptUser already on addRemove list\n");
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
            "asynManager:removeInterruptUser not on list\n");
        return asynError;
    }
    while(pinterruptBase->callbackActive) {
        if(pinterruptNodePvt->isOnAddRemoveList) {
            epicsMutexUnlock(pport->asynManagerLock);
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                "asynManager:removeInterruptUser already on addRemove list\n");
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
    userPvt    *puserPvt = asynUserToUserPvt(pasynUser);
    tracePvt   *ptracePvt = findTracePvt(puserPvt);

    ptracePvt->traceMask = mask;
    if(puserPvt->pport) exceptionOccurred(pasynUser,asynExceptionTraceMask);
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
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    tracePvt *ptracePvt  = findTracePvt(puserPvt);

    ptracePvt->traceIOMask = mask;
    if(puserPvt->pport) exceptionOccurred(pasynUser,asynExceptionTraceIOMask);
    return asynSuccess;
}

static int getTraceIOMask(asynUser *pasynUser)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    tracePvt *ptracePvt  = findTracePvt(puserPvt);

    return ptracePvt->traceIOMask;
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
                "asynManager:setTraceFile fclose error %s\n",strerror(errno));
        }
    }
    if(fp==0) {
        ptracePvt->type = traceFileErrlog; ptracePvt->fp = 0;
    } else if(fp==stdout) {
        ptracePvt->type = traceFileStdout; ptracePvt->fp = 0;
    } else if(fp==stderr) {
        ptracePvt->type = traceFileStderr; ptracePvt->fp = 0;
    } else {
        ptracePvt->fp = fp;
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

static asynStatus setTraceIOTruncateSize(asynUser *pasynUser,int size)
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

static int getTraceIOTruncateSize(asynUser *pasynUser)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    tracePvt *ptracePvt  = findTracePvt(puserPvt);

    return ptracePvt->traceTruncateSize;
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

static int tracePrint(asynUser *pasynUser,int reason, const char *pformat, ...)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    tracePvt *ptracePvt  = findTracePvt(puserPvt);
    va_list  pvar;
    int      nout = 0;
    FILE     *fp;

    if(!(reason&ptracePvt->traceMask)) return 0;
    epicsMutexMustLock(pasynBase->lockTrace);
    fp = getTraceFile(pasynUser);
    nout += printTime(fp);
    va_start(pvar,pformat);
    if(fp) {
        nout = vfprintf(fp,pformat,pvar);
    } else {
        nout += errlogVprintf(pformat,pvar);
    }
    va_end(pvar);
    if(fp==stdout || fp==stderr) fflush(fp);
    epicsMutexUnlock(pasynBase->lockTrace);
    return nout;
}

static int tracePrintIO(asynUser *pasynUser,int reason,
    const char *buffer, int len,const char *pformat, ...)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    tracePvt *ptracePvt  = findTracePvt(puserPvt);
    va_list  pvar;
    int      nout = 0;
    FILE     *fp;
    int traceMask,traceIOMask,traceTruncateSize,nBytes;

    traceMask = ptracePvt->traceMask;
    traceIOMask = ptracePvt->traceIOMask;
    traceTruncateSize = ptracePvt->traceTruncateSize;
    if(!(reason&traceMask)) return 0;
    epicsMutexMustLock(pasynBase->lockTrace);
    fp = getTraceFile(pasynUser);
    nout += printTime(fp);
    va_start(pvar,pformat);
    if(fp) {
        nout = vfprintf(fp,pformat,pvar);
    } else {
        nout += errlogVprintf(pformat,pvar);
    }
    va_end(pvar);
    nBytes = (len<traceTruncateSize) ? len : traceTruncateSize;
    if((traceIOMask&ASYN_TRACEIO_ASCII) && (nBytes>0)) {
       if(fp) {
           nout += fprintf(fp,"%.*s\n",nBytes,buffer);
       } else {
           nout += errlogPrintf("%.*s\n",nBytes,buffer);
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
        int i;
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
    if(fp==stdout || fp==stderr) fflush(fp);
    epicsMutexUnlock(pasynBase->lockTrace);
    return nout;
}
