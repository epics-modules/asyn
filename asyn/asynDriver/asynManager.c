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

struct tracePvt {
    int  traceMask;
    int  traceIOMask;
    FILE *fp;
    int  traceTruncateSize;
    int  traceBufferSize;
    char *traceBuffer;
};

typedef struct asynBase {
    ELLLIST           asynPortList;
    epicsTimerQueueId timerQueue;
    epicsMutexId      lockTrace;
    tracePvt          trace;
}asynBase;
static asynBase *pasynBase = 0;

typedef struct interfaceNode {
    ELLNODE       node;
    asynInterface *pasynInterface;
}interfaceNode;

typedef struct dpCommon { /*device/port common fields*/
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
}dpCommon;

typedef struct exceptionUser {
    ELLNODE           node;
    ELLNODE           notifyNode;
    exceptionCallback callback;
    asynUser          *pasynUser;
    epicsEventId      notify;
}exceptionUser;

struct userPvt {
    ELLNODE      node;        /*For asynPort.queueList*/
    exceptionUser *pexceptionUser;
    userCallback queueCallback;
    userCallback timeoutCallback;
    BOOL         isQueued;
    unsigned int lockCount;
    epicsTimerId timer;
    double       timeout; /*For queueRequest*/
    port         *pport;
    device       *pdevice;
    asynUser     user;
};

struct device {
    ELLNODE   node;     /*For asynPort.deviceList*/
    dpCommon  dpc;
    int       addr;
};

struct port {
    ELLNODE       node;  /*For asynBase.asynPortList*/
    char          *portName;
    BOOL          multiDevice;
    epicsMutexId  lock;
    dpCommon      dpc;
    asynUser      *pasynUser; /*For portThread autoConnect*/
    ELLLIST       queueList[NUMBER_QUEUE_PRIORITIES];
    ELLLIST       deviceList;
    ELLLIST       interfaceList;
    BOOL          queueStateChange;
    epicsEventId  notifyPortThread;
    epicsThreadId threadid;
};

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
static void dpCommonInit(dpCommon *pdpCommon,BOOL autoConnect);
static dpCommon *findDpCommon(userPvt *puserPvt);
static tracePvt *findTracePvt(userPvt *puserPvt);
static port *locatePort(const char *portName);
static device *locateDevice(port *pport,int addr,BOOL allocNew);
static interfaceNode *locateInterfaceNode(
            ELLLIST *plist,const char *interfaceType,BOOL allocNew);
static void queueTimeoutCallback(void *);
static void exceptionOccurred(asynUser *pasynUser,asynException exception);
static void autoConnect(port *pport,int addr);
static void portThread(port *pport);
    
/* asynManager methods */
static void report(FILE *fp,int details);
static asynUser *createAsynUser(userCallback queue, userCallback timeout);
static asynStatus freeAsynUser(asynUser *pasynUser);
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
static asynStatus lock(asynUser *pasynUser);
static asynStatus unlock(asynUser *pasynUser);
static asynStatus getAddr(asynUser *pasynUser,int *addr);
static asynStatus registerPort(const char *portName,
    int multiDevice,int autoConnect,
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

static asynManager manager = {
    report,
    createAsynUser,
    freeAsynUser,
    isMultiDevice,
    connectDevice,
    disconnect,
    exceptionCallbackAdd,
    exceptionCallbackRemove,
    findInterface,
    queueRequest,
    cancelRequest,
    lock,
    unlock,
    getAddr,
    registerPort,
    registerInterface,
    exceptionConnect,
    exceptionDisconnect,
    interposeInterface,
    enable,
    autoConnectAsyn,
    isConnected,
    isEnabled,
    isAutoConnect
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
}
static void asynInit(void)
{
    if(pasynBase) return;
    pasynBase = callocMustSucceed(1,sizeof(asynBase),"asynInit");
    ellInit(&pasynBase->asynPortList);
    pasynBase->timerQueue = epicsTimerQueueAllocate(
        1,epicsThreadPriorityScanLow);
    pasynBase->lockTrace = epicsMutexMustCreate();
    tracePvtInit(&pasynBase->trace);
}

static void dpCommonInit(dpCommon *pdpCommon,BOOL autoConnect)
{
    pdpCommon->enabled = TRUE;
    pdpCommon->connected = FALSE;
    pdpCommon->autoConnect = autoConnect;
    ellInit(&pdpCommon->interposeInterfaceList);
    ellInit(&pdpCommon->exceptionUserList);
    ellInit(&pdpCommon->exceptionNotifyList);
    tracePvtInit(&pdpCommon->trace);
}

static dpCommon *findDpCommon(userPvt *puserPvt)
{
    port *pport = puserPvt->pport;
    device *pdevice = puserPvt->pdevice;

    if(!pport) return(0);
    if(!pport->multiDevice || !pdevice) return(&pport->dpc);
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
    pport = (port *)ellFirst(&pasynBase->asynPortList);
    while(pport) {
        if(strcmp(pport->portName,portName)==0) break;
        pport = (port *)ellNext(&pport->node);
    }
    return pport;
}

static device *locateDevice(port *pport,int addr,BOOL allocNew)
{
    device *pdevice;

    assert(pport);
    if(!pport->multiDevice || addr < 0) return(0);
    pdevice = (device *)ellFirst(&pport->deviceList);
    while(pdevice) {
        if(pdevice->addr == addr) return pdevice;
        pdevice = (device *)ellNext(&pdevice->node);
    }
    if(!pdevice && allocNew) {
        pdevice = callocMustSucceed(1,sizeof(device),
            "asynManager:locateDevice");
        pdevice->addr = addr;
        dpCommonInit(&pdevice->dpc,pport->dpc.autoConnect);
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

static void queueTimeoutCallback(void *pvt)
{
    userPvt    *puserPvt = (userPvt *)pvt;
    asynUser   *pasynUser = &puserPvt->user;
    asynStatus status;
    int        wasQueued;

    status = cancelRequest(pasynUser,&wasQueued);
    if(status!=asynSuccess) {
         asynPrint(pasynUser,ASYN_TRACE_ERROR,
             "asynManager:queueTimeoutCallback cancelRequest error. Why?\n");
    } else if(wasQueued && puserPvt->timeoutCallback){
        puserPvt->timeoutCallback(pasynUser);
    }
}

/* While an exceptionActive exceptionCallbackAdd and exceptionCallbackRemove
   will wait to be notified that exceptionActive is no longer true.
*/

static void exceptionOccurred(asynUser *pasynUser,asynException exception)
{
    userPvt    *puserPvt = asynUserToUserPvt(pasynUser);
    port       *pport = puserPvt->pport;
    device     *pdevice = puserPvt->pdevice;
    int        addr = (pdevice ? pdevice->addr : -1);
    dpCommon   *pdpCommon = findDpCommon(puserPvt);
    exceptionUser *pexceptionUser;

    assert(pport&&pdpCommon);
    epicsMutexMustLock(pport->lock);
    pdpCommon->exceptionActive = TRUE;
    epicsMutexUnlock(pport->lock);
    pexceptionUser = (exceptionUser *)ellFirst(&pdpCommon->exceptionUserList);
    while(pexceptionUser) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s addr %d asynManager:exceptionOccurred calling exceptionUser\n",
            pport->portName,addr, (int)exception);
        pexceptionUser->callback(pexceptionUser->pasynUser,exception);
        pexceptionUser = (exceptionUser *)ellNext(&pexceptionUser->node);
    }
    epicsMutexMustLock(pport->lock);
    pexceptionUser  = (exceptionUser *)ellFirst(&pdpCommon->exceptionNotifyList);
    while(pexceptionUser) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s addr %d asynManager:exceptionOccurred notify\n",
            pport->portName,addr, (int)exception);
        epicsEventSignal(pexceptionUser->notify);
        ellDelete(&pdpCommon->exceptionNotifyList,&pexceptionUser->notifyNode);
    }
    pdpCommon->exceptionActive = FALSE;
    pport->queueStateChange = TRUE;
    epicsMutexUnlock(pport->lock);
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
            "%s addr %d asynManager:autoConnect connectDevice failed.\n",
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
            "%s addr %d asynManager:autoConnect findInterface failed.\n",
            pport->portName,addr);
            goto disconnect;
    }
    pasynCommon = (asynCommon *)pasynInterface->pinterface;
    drvPvt = pasynInterface->drvPvt;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "asynManager:autoConnect port:%s addr %d\n",pport->portName,addr);
    pasynUser->errorMessage[0] = '\0';
    status = pasynCommon->connect(drvPvt,pasynUser);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "asynManager:autoConnect could not connect %s. port:%s addr %d\n",
            pasynUser->errorMessage,pport->portName,addr);
    }
disconnect:
    status = pasynManager->disconnect(pasynUser);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s addr %d asynManager:autoConnect disconnect failed.\n",
            pport->portName,addr);
    }
}

static void portThread(port *pport)
{
    userPvt  *puserPvt;
    asynUser *pasynUser;
    asynStatus status;

    taskwdInsert(epicsThreadGetIdSelf(),0,0);
    while(1) {
        epicsEventMustWait(pport->notifyPortThread);
        epicsMutexMustLock(pport->lock);
        if(!pport->dpc.enabled) {
            epicsMutexUnlock(pport->lock);
            continue;
        }
        /*Process ALL connect/disconnect requests first*/
        while((puserPvt = (userPvt *)ellFirst(
        &pport->queueList[asynQueuePriorityConnect]))) {
            assert(puserPvt->isQueued);
            ellDelete(&pport->queueList[asynQueuePriorityConnect],&puserPvt->node);
            puserPvt->isQueued = FALSE;
            pasynUser = userPvtToAsynUser(puserPvt);
            if(puserPvt->timer && puserPvt->timeout>0.0) {
                epicsTimerCancel(puserPvt->timer);
            }
            pasynUser->errorMessage[0] = '\0';
            asynPrint(pasynUser,ASYN_TRACE_FLOW,
                "asynManager connect queueCallback port:%s\n",pport->portName);
            epicsMutexUnlock(pport->lock);
            puserPvt->queueCallback(pasynUser);
            epicsMutexMustLock(pport->lock);
        }
        if(!pport->dpc.connected && pport->dpc.autoConnect) {
            epicsMutexUnlock(pport->lock);
            autoConnect(pport,-1);
            epicsMutexMustLock(pport->lock);
        }
        if(!pport->dpc.connected) {
            epicsMutexUnlock(pport->lock);
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
                    if(pdpCommon->enabled) {
                        if(!pdpCommon->connected && pdpCommon->autoConnect) {
                            int addr;
                            asynUser *pasynUser = &puserPvt->user;
                            status = pasynManager->getAddr(pasynUser,&addr);
                            if(status!=asynSuccess) {
                                asynPrint(pasynUser,ASYN_TRACE_ERROR,
                                    "%s\n",pasynUser->errorMessage);
                            } else if(addr>=0) {
                                epicsMutexUnlock(pport->lock);
                                autoConnect(pport,addr);
                                epicsMutexMustLock(pport->lock);
                                if(pport->queueStateChange) break;/*while(puserPvt)*/
                            }
                        }
                        if(pdpCommon->connected) {
                            if(!pdpCommon->plockHolder
                            || pdpCommon->plockHolder==puserPvt) {
                                assert(puserPvt->isQueued);
                                ellDelete(&pport->queueList[i],&puserPvt->node);
                                puserPvt->isQueued = FALSE;
                                break; /*while(puserPvt)*/
                            }
                        }
                    }
                    puserPvt = (userPvt *)ellNext(&puserPvt->node);
                }
                if(puserPvt || pport->queueStateChange) break; /*for*/
            }
            if(!puserPvt || pport->queueStateChange) break; /*while(1)*/
            pasynUser = userPvtToAsynUser(puserPvt);
            if(puserPvt->lockCount>0) pdpCommon->plockHolder = puserPvt;
            if(puserPvt->timer && puserPvt->timeout>0.0) {
                epicsTimerCancel(puserPvt->timer);
            }
            pasynUser->errorMessage[0] = '\0';
            asynPrint(pasynUser,ASYN_TRACE_FLOW,
                "asynManager queueCallback port:%s\n",pport->portName);
            epicsMutexUnlock(pport->lock);
            puserPvt->queueCallback(pasynUser);
            epicsMutexMustLock(pport->lock);
            if(pport->queueStateChange) break;
        }
        epicsMutexUnlock(pport->lock);
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

static void report(FILE *fp,int details)
{
    port *pport;

    if(!pasynBase) asynInit();
    pport = (port *)ellFirst(&pasynBase->asynPortList);
    while(pport) {
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
	fprintf(fp,"%s multiDevice:%s autoConnect:%s enabled:%s connected:%s"
            " numberConnects %lu\n",
            pport->portName,(pport->multiDevice ? "Yes" : "No"),
            (pdpc->autoConnect ? "Yes" : "No"),
            (pdpc->enabled ? "Yes" : "No"),
            (pdpc->connected ? "Yes" : "No"),
            pdpc->numberConnects);
        epicsMutexMustLock(pport->lock);
        lockCount = (pdpc->plockHolder) ? pdpc->plockHolder->lockCount : 0;
        epicsMutexUnlock(pport->lock);
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
            epicsMutexMustLock(pport->lock);
            lockCount = (pdpc->plockHolder) ? pdpc->plockHolder->lockCount : 0;
            epicsMutexUnlock(pport->lock);
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
        pport = (port *)ellNext(&pport->node);
    }
}

static asynUser *createAsynUser(userCallback queue, userCallback timeout)
{
    userPvt  *puserPvt;
    asynUser *pasynUser;
    int      nbytes;

    if(!pasynBase) asynInit();
    nbytes = sizeof(userPvt) + ERROR_MESSAGE_SIZE + 1;
    puserPvt = callocMustSucceed(1,nbytes,"asynCommon:registerDriver");
    puserPvt->queueCallback = queue;
    pasynUser = userPvtToAsynUser(puserPvt);
    pasynUser->errorMessage = (char *)(pasynUser +1);
    pasynUser->errorMessageSize = ERROR_MESSAGE_SIZE;
    if(timeout) {
        puserPvt->timeoutCallback = timeout;
        puserPvt->timer = epicsTimerQueueCreateTimer(
            pasynBase->timerQueue,queueTimeoutCallback,puserPvt);
    }
    return pasynUser;
}

static asynStatus freeAsynUser(asynUser *pasynUser)
{
    userPvt *puserPvt = asynUserToUserPvt(pasynUser);

    if(puserPvt->pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:freeAsynUser asynUser is connected\n");
        return asynError;
    }
    if(puserPvt->timer)
        epicsTimerQueueDestroyTimer(pasynBase->timerQueue,puserPvt->timer);
    free(puserPvt);
    return asynSuccess;
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
    *yesNo = (int)pport->multiDevice;
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
    epicsMutexMustLock(pport->lock);
    puserPvt->pport = pport;
    if(addr>=0) {
        pdevice = locateDevice(pport,addr,TRUE);
        puserPvt->pdevice = pdevice;
    }
    epicsMutexUnlock(pport->lock);
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
    epicsMutexMustLock(pport->lock);
    if(puserPvt->isQueued) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::disconnect: isQueued\n");
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
    epicsMutexUnlock(pport->lock);
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
    epicsMutexMustLock(pport->lock);
    pexceptionUser = puserPvt->pexceptionUser;
    if(pexceptionUser) {
        epicsMutexUnlock(pport->lock);
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
        epicsMutexUnlock(pport->lock);
        epicsEventMustWait(pexceptionUser->notify);
        epicsMutexMustLock(pport->lock);
    }
    puserPvt->pexceptionUser = pexceptionUser;
    ellAdd(&pdpCommon->exceptionUserList,&pexceptionUser->node);
    epicsMutexUnlock(pport->lock);
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
    epicsMutexMustLock(pport->lock);
    pexceptionUser = puserPvt->pexceptionUser;
    if(!pexceptionUser) {
        epicsMutexUnlock(pport->lock);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:exceptionCallbackRemove not on list\n");
        return asynError;
    }
    while(pdpCommon->exceptionActive) {
        ellAdd(&pdpCommon->exceptionNotifyList,&pexceptionUser->notifyNode);
        epicsMutexUnlock(pport->lock);
        epicsEventMustWait(pexceptionUser->notify);
        epicsMutexMustLock(pport->lock);
    }
    puserPvt->pexceptionUser = 0;
    ellDelete(&pdpCommon->exceptionUserList,&pexceptionUser->node);
    epicsEventDestroy(pexceptionUser->notify);
    free(pexceptionUser);
    epicsMutexUnlock(pport->lock);
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
    epicsMutexMustLock(pport->lock);
    if(puserPvt->isQueued) {
        epicsMutexUnlock(pport->lock);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::queueRequest is already queued\n");
        return asynError;
    }
    if(pdpCommon->plockHolder && pdpCommon->plockHolder==puserPvt) {
        /*Add to front of list*/
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s addr %d asynManager:queueRequest priority %d from lockHolder\n",
            pport->portName,addr,priority);
        ellInsert(&pport->queueList[priority],0,&puserPvt->node);
    } else {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s addr %d asynManager:queueRequest priority %d not lockHolder\n",
            pport->portName,addr,priority);
        /*Add to end of list*/
        ellAdd(&pport->queueList[priority],&puserPvt->node);
    }
    pport->queueStateChange = TRUE;
    puserPvt->isQueued = TRUE;
    puserPvt->timeout = timeout;
    if(puserPvt->timeout>0.0) {
        if(!puserPvt->timeoutCallback) {
            printf("%s,%d queueRequest with timeout but no timeout callback\n",
                pport->portName,pdevice->addr);
        } else {
            epicsTimerStartDelay(puserPvt->timer,puserPvt->timeout);
        }
    }
    epicsMutexUnlock(pport->lock);
    epicsEventSignal(pport->notifyPortThread);
    return asynSuccess;
}

static asynStatus cancelRequest(asynUser *pasynUser,int *wasQueued)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    port     *pport = puserPvt->pport;
    device   *pdevice = puserPvt->pdevice;
    int      addr = (pdevice ? pdevice->addr : -1);
    int      i;

    *wasQueued = 0; /*Initialize to not removed*/
    if(!pport) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "asynManager:cancelRequest but not connected\n");
        return asynError;
    }
    epicsMutexMustLock(pport->lock);
    if(!puserPvt->isQueued) {
        epicsMutexUnlock(pport->lock);
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s addr %d asynManager:cancelRequest but not queued\n",
            pport->portName,addr);
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
    if(puserPvt) {
        puserPvt->isQueued = FALSE;
        pport->queueStateChange = TRUE;
        if(puserPvt->timer && puserPvt->timeout>0.0) {
            epicsTimerCancel(puserPvt->timer);
        }
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s addr %d asynManager:cancelRequest\n",
            pport->portName,addr);
    } else {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s addr %d asynManager:cancelRequest LOGIC ERROR\n",
            pport->portName, addr);
        epicsMutexUnlock(pport->lock);
        return asynError;
    }
    epicsMutexUnlock(pport->lock);
    epicsEventSignal(pport->notifyPortThread);
    return asynSuccess;
}

static asynStatus lock(asynUser *pasynUser)
{
    userPvt *puserPvt = asynUserToUserPvt(pasynUser);
    port    *pport = puserPvt->pport;

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
    epicsMutexMustLock(pport->lock);
    puserPvt->lockCount++;
    epicsMutexUnlock(pport->lock);
    return asynSuccess;
}

static asynStatus unlock(asynUser *pasynUser)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    port     *pport = puserPvt->pport;
    dpCommon *pdpCommon = findDpCommon(puserPvt);
    BOOL     wasOwner = FALSE;

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
    if(puserPvt->lockCount==0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager::unlock but not locked\n");
        return asynError;
    }
    epicsMutexMustLock(pport->lock);
    puserPvt->lockCount--;
    if(puserPvt->lockCount==0 && pdpCommon->plockHolder==puserPvt) {
        pdpCommon->plockHolder = 0;
        wasOwner = TRUE;
    }
    epicsMutexUnlock(pport->lock);
    if(wasOwner) epicsEventSignal(pport->notifyPortThread);
    return asynSuccess;
}

static asynStatus getAddr(asynUser *pasynUser,int *addr)
{
    userPvt *puserPvt = asynUserToUserPvt(pasynUser);
    port    *pport = puserPvt->pport;

    if(!pport) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynManager:getAddr no connected to device");
        return asynError;
    }
    if(!pport->multiDevice || !puserPvt->pdevice) {
        *addr = -1;
    } else {
        *addr = puserPvt->pdevice->addr;
    }
    return asynSuccess;
}

static asynStatus registerPort(const char *portName,
    int multiDevice,int autoConnect,
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
    pport->multiDevice = multiDevice;
    pport->lock = epicsMutexMustCreate();
    dpCommonInit(&pport->dpc,autoConnect);
    pport->pasynUser = createAsynUser(0,0);
    for(i=0; i<NUMBER_QUEUE_PRIORITIES; i++) ellInit(&pport->queueList[i]);
    ellInit(&pport->deviceList);
    ellInit(&pport->interfaceList);
    pport->notifyPortThread = epicsEventMustCreate(epicsEventEmpty);
    ellAdd(&pasynBase->asynPortList,&pport->node);
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
    epicsMutexMustLock(pport->lock);
    pinterfaceNode = locateInterfaceNode(
        &pport->interfaceList,pasynInterface->interfaceType,TRUE);
    if(pinterfaceNode->pasynInterface) {
        printf("interface %s already registered for port %s\n",
            pasynInterface->interfaceType,pport->portName);
        epicsMutexUnlock(pport->lock);
        return asynError;
    }
    pinterfaceNode->pasynInterface = pasynInterface;
    epicsMutexUnlock(pport->lock);
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
    epicsMutexMustLock(pport->lock);
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
    *ppPrev = pPrev;
    pinterfaceNode->pasynInterface = pasynInterface;
    epicsMutexUnlock(pport->lock);
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
    if(ptracePvt->fp!=0 && ptracePvt->fp!=stdout && ptracePvt->fp!=stderr) {
        int status;

        errno = 0;
        status = fclose(ptracePvt->fp);
        if(status) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "asynManager:setTraceFile fclose error %s\n",strerror(errno));
        }
    }
    ptracePvt->fp = fp;
    if(puserPvt->pport) exceptionOccurred(pasynUser,asynExceptionTraceFile);
    epicsMutexUnlock(pasynBase->lockTrace);
    return asynSuccess;
}

static FILE *getTraceFile(asynUser *pasynUser)
{
    userPvt  *puserPvt = asynUserToUserPvt(pasynUser);
    tracePvt *ptracePvt  = findTracePvt(puserPvt);

    return ptracePvt->fp;
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
    fp = ptracePvt->fp;
    nout += printTime(fp);
    va_start(pvar,pformat);
    if(fp) {
        nout = vfprintf(fp,pformat,pvar);
    } else {
        nout += errlogVprintf(pformat,pvar);
    }
    va_end(pvar);
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
    fp = ptracePvt->fp;
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
    epicsMutexUnlock(pasynBase->lockTrace);
    return nout;
}
