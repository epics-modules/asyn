/*interruptDriver.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*test driver for asyn support*/
/* 
 * Author: Marty Kraimer
 */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <cantProceed.h>
#include <ellLib.h>
#include <epicsStdio.h>
#include <epicsThread.h>
#include <epicsMutex.h>
#include <iocsh.h>

#include <asynDriver.h>
#include <asynInt32.h>
#include <asynInt32Callback.h>
#include <epicsExport.h>


typedef struct callbackNode {
    ELLNODE node;
    void (*callback)(void *userPvt, epicsInt32 data);
    void    *userPvt;
}callbackNode;

typedef struct interruptPvt {
    const char    *portName;
    double        delay;
    asynInterface common;
    asynInterface int32;
    asynInterface int32Callback;
    epicsInt32   value;
    int          connected;
    ELLLIST      callbackList;
    epicsMutexId lock;
}interruptPvt;
    
/* init routine */
static int interruptInt32DriverInit(const char *dn, double delay,
    int noAutoConnect);

static void interruptThread(interruptPvt *pinterruptPvt);

/* asynCommon methods */
static void report(void *drvPvt,FILE *fp,int details);
static asynStatus connect(void *drvPvt,asynUser *pasynUser);
static asynStatus disconnect(void *drvPvt,asynUser *pasynUser);
static asynCommon asyn = { report, connect, disconnect };

/* asynInt32 methods */
static asynStatus interruptWrite(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 value);
static asynStatus interruptRead(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 *value);
static asynStatus getBounds(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 *low, epicsInt32 *high);
static asynInt32 int32 = {
    interruptWrite, interruptRead, getBounds
};

/* asynInt32Callback methods */
static asynStatus registerCallback(void *drvPvt, asynUser *pasynUser,
            void (*callback)(void *userPvt, epicsInt32 data),
            void *userPvt);
static asynStatus cancelCallback(void *drvPvt, asynUser *pasynUser,
            void (*callback)(void *userPvt, epicsInt32 data),
            void *userPvt);
static asynInt32Callback int32Callback = { registerCallback,cancelCallback};

static int interruptInt32DriverInit(const char *dn, double delay,
    int noAutoConnect)
{
    interruptPvt *pinterruptPvt;
    char         *portName;
    asynStatus   status;
    int          nbytes;

    nbytes = sizeof(interruptPvt) + strlen(dn) + 1;
    pinterruptPvt = callocMustSucceed(nbytes,sizeof(char),"interruptDriverInit");
    portName = (char *)(pinterruptPvt + 1);
    strcpy(portName,dn);
    pinterruptPvt->portName = portName;
    if(delay<.05) delay = .05;
    pinterruptPvt->delay = delay;
    pinterruptPvt->common.interfaceType = asynCommonType;
    pinterruptPvt->common.pinterface  = (void *)&asyn;
    pinterruptPvt->common.drvPvt = pinterruptPvt;
    pinterruptPvt->int32.interfaceType = asynInt32Type;
    pinterruptPvt->int32.pinterface  = (void *)&int32;
    pinterruptPvt->int32.drvPvt = pinterruptPvt;
    pinterruptPvt->int32Callback.interfaceType = asynInt32CallbackType;
    pinterruptPvt->int32Callback.pinterface  = (void *)&int32Callback;
    pinterruptPvt->int32Callback.drvPvt = pinterruptPvt;
    pinterruptPvt->lock = epicsMutexMustCreate();
    ellInit(&pinterruptPvt->callbackList);
    status = pasynManager->registerPort(portName,0,!noAutoConnect,0,0);
    if(status!=asynSuccess) {
        printf("interruptDriverInit registerDriver failed\n");
        return 0;
    }
    status = pasynManager->registerInterface(portName,&pinterruptPvt->common);
    if(status!=asynSuccess){
        printf("interruptDriverInit registerInterface failed\n");
        return 0;
    }
    status = pasynManager->registerInterface(portName,&pinterruptPvt->int32);
    if(status!=asynSuccess){
        printf("interruptDriverInit registerInterface failed\n");
        return 0;
    }
    status = pasynManager->registerInterface(portName,&pinterruptPvt->int32Callback);
    if(status!=asynSuccess){
        printf("interruptDriverInit registerInterface failed\n");
        return 0;
    }
    epicsThreadCreate("interruptInt32",
        epicsThreadPriorityHigh,
        epicsThreadGetStackSize(epicsThreadStackSmall),
        (EPICSTHREADFUNC)interruptThread,pinterruptPvt);
    return(0);
}

static void interruptThread(interruptPvt *pinterruptPvt)
{
    callbackNode *pcallbackNode;
    while(1) {
        if(pinterruptPvt->value>=10.0) {
            pinterruptPvt->value = 0;
        } else {
            pinterruptPvt->value++;
        }
        epicsMutexMustLock(pinterruptPvt->lock);
        pcallbackNode = (callbackNode *)ellFirst(&pinterruptPvt->callbackList);
        while(pcallbackNode) {
            pcallbackNode->callback(pcallbackNode->userPvt,pinterruptPvt->value);
            pcallbackNode = (callbackNode *)ellNext(&pcallbackNode->node);
        }
        epicsMutexUnlock(pinterruptPvt->lock);
        epicsThreadSleep(pinterruptPvt->delay);
    }
}

/* asynCommon methods */
static void report(void *drvPvt,FILE *fp,int details)
{
    interruptPvt *pinterruptPvt = (interruptPvt *)drvPvt;

    fprintf(fp,"    interruptDriver. connected:%s delay = %f\n",
        (pinterruptPvt->connected ? "Yes" : "No"),
        pinterruptPvt->delay);
}

static asynStatus connect(void *drvPvt,asynUser *pasynUser)
{
    interruptPvt *pinterruptPvt = (interruptPvt *)drvPvt;

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s interruptDriver:connect\n",pinterruptPvt->portName);
    if(pinterruptPvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
           "%s interruptDriver:connect port already connected\n",
           pinterruptPvt->portName);
        return asynError;
    }
    pinterruptPvt->connected = 1;
    pasynManager->exceptionConnect(pasynUser);
    return asynSuccess;
}

static asynStatus disconnect(void *drvPvt,asynUser *pasynUser)
{
    interruptPvt *pinterruptPvt = (interruptPvt *)drvPvt;

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s interruptDriver:disconnect\n",pinterruptPvt->portName);
    if(!pinterruptPvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
           "%s interruptDriver:disconnect port not connected\n",
           pinterruptPvt->portName);
        return asynError;
    }
    pinterruptPvt->connected = 0;
    pasynManager->exceptionDisconnect(pasynUser);
    return asynSuccess;
}

static asynStatus interruptWrite(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 value)
{
    interruptPvt *pinterruptPvt = (interruptPvt *)drvPvt;

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s interruptDriver:writeInt32 value %d\n",
        pinterruptPvt->portName,value);
    if(!pinterruptPvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s interruptDriver:read not connected\n",
            pinterruptPvt->portName);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s interruptDriver:read not connected\n",
            pinterruptPvt->portName);
        return asynError;
    }
    pinterruptPvt->value = value;
    asynPrint(pasynUser,ASYN_TRACEIO_DRIVER,
        "%s write %d\n",pinterruptPvt->portName,value);
    return asynSuccess;
}

static asynStatus interruptRead(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 *value)
{
    interruptPvt *pinterruptPvt = (interruptPvt *)drvPvt;

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s interruptDriver:readInt32 value %d\n",
        pinterruptPvt->portName,value);
    if(!pinterruptPvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s interruptDriver:read not connected\n",
            pinterruptPvt->portName);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s interruptDriver:read not connected\n",
            pinterruptPvt->portName);
        return asynError;
    }
    *value = pinterruptPvt->value;
    asynPrint(pasynUser,ASYN_TRACEIO_DRIVER,
        "%s read %d\n",pinterruptPvt->portName,value);
    return asynSuccess;
}

static asynStatus getBounds(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 *low, epicsInt32 *high)
{
    *low = *high = 0;
    return asynSuccess;
}

static asynStatus registerCallback(void *drvPvt, asynUser *pasynUser,
            void (*callback)(void *userPvt, epicsInt32 data),
            void *userPvt)
{
    interruptPvt *pinterruptPvt = (interruptPvt *)drvPvt;
    callbackNode *pcallbackNode;

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s interruptDriver:registerCallback\n",pinterruptPvt->portName);
    pcallbackNode = callocMustSucceed(1,sizeof(callbackNode),"interruptInt32");
    pcallbackNode->callback = callback;
    pcallbackNode->userPvt = userPvt;
    epicsMutexMustLock(pinterruptPvt->lock);
    ellAdd(&pinterruptPvt->callbackList,&pcallbackNode->node);
    epicsMutexUnlock(pinterruptPvt->lock);
    return(asynSuccess);
}

static asynStatus cancelCallback(void *drvPvt, asynUser *pasynUser,
            void (*callback)(void *userPvt, epicsInt32 data),
            void *userPvt)
{
    interruptPvt *pinterruptPvt = (interruptPvt *)drvPvt;
    callbackNode *pcallbackNode;

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s interruptDriver:cancelCallback\n",pinterruptPvt->portName);
    epicsMutexMustLock(pinterruptPvt->lock);
    pcallbackNode = (callbackNode *)ellFirst(&pinterruptPvt->callbackList);
    while(pcallbackNode) {
        if(pcallbackNode->callback!=callback||pcallbackNode->userPvt!=userPvt) 
            break;
        pcallbackNode = (callbackNode *)ellNext(&pcallbackNode->node);
    }
    if(!pcallbackNode) {
        epicsMutexUnlock(pinterruptPvt->lock);
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s interruptDriver:cancelCallback but not registered\n",
            pinterruptPvt->portName);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s cancelCallback but not registered\n",
            pinterruptPvt->portName);
        return asynError;
    }
    ellDelete(&pinterruptPvt->callbackList,&pcallbackNode->node);
    epicsMutexUnlock(pinterruptPvt->lock);
    free(pcallbackNode);
    return(asynSuccess);
}

/* register interruptDriverInit*/
static const iocshArg interruptDriverInitArg0 = { "portName", iocshArgString };
static const iocshArg interruptDriverInitArg1 = { "delay", iocshArgDouble };
static const iocshArg interruptDriverInitArg2 = { "disable auto-connect", iocshArgInt };
static const iocshArg *interruptDriverInitArgs[] = {
    &interruptDriverInitArg0,&interruptDriverInitArg1,
    &interruptDriverInitArg2};
static const iocshFuncDef interruptDriverInitFuncDef = {
    "interruptInt32DriverInit", 3, interruptDriverInitArgs};
static void interruptDriverInitCallFunc(const iocshArgBuf *args)
{
    interruptInt32DriverInit(args[0].sval,args[1].dval,args[2].ival);
}

static void interruptInt32DriverRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&interruptDriverInitFuncDef, interruptDriverInitCallFunc);
    }
}
epicsExportRegistrar(interruptInt32DriverRegister);
