/*int32Driver.c */
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
#include <ctype.h>

#include <cantProceed.h>
#include <epicsStdio.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <epicsThread.h>
#include <iocsh.h>

#include <asynDriver.h>
#include <asynDrvUser.h>
#include <asynInt32.h>
#include <asynFloat64.h>

#include <epicsExport.h>
#define NCHANNELS 16

typedef struct chanPvt {
    epicsInt32 value;
    void       *asynPvt;
}chanPvt;

typedef struct drvPvt {
    const char    *portName;
    epicsMutexId  lock;
    epicsEventId  waitWork;
    int           connected;
    double        interruptDelay;
    asynInterface common;
    asynInterface asynDrvUser;
    asynInterface asynInt32;
    asynInterface asynFloat64;
    epicsInt32    low;
    epicsInt32    high;
    void          *asynInt32Pvt; /* For registerInterruptSource*/
    void          *asynFloat64Pvt; /* For registerInterruptSource*/
    chanPvt       channel[NCHANNELS];
}drvPvt;

static int int32DriverInit(const char *dn,int low,int high);
static asynStatus getAddr(drvPvt *pdrvPvt,asynUser *pasynUser,
    int *paddr,int portOK);
static void interruptThread(drvPvt *pdrvPvt);

/* asynCommon methods */
static void report(void *drvPvt,FILE *fp,int details);
static asynStatus connect(void *drvPvt,asynUser *pasynUser);
static asynStatus disconnect(void *drvPvt,asynUser *pasynUser);
static asynCommon common = { report, connect, disconnect };

/* asynDrvUser */
static asynStatus create(void *drvPvt,asynUser *pasynUser,
    const char *drvInfo, const char **pptypeName,size_t *psize);
static asynStatus getType(void *drvPvt,asynUser *pasynUser,
    const char **pptypeName,size_t *psize);
static asynStatus destroy(void *drvPvt,asynUser *pasynUser);
static asynDrvUser drvUser = {create,getType,destroy};

/* asynInt32 methods */
static asynStatus int32Write(void *drvPvt,asynUser *pasynUser,epicsInt32 value);
static asynStatus int32Read(void *drvPvt,asynUser *pasynUser,epicsInt32 *value);
static asynStatus int32GetBounds(void *drvPvt, asynUser *pasynUser,
                                epicsInt32 *low, epicsInt32 *high);

/* asynFloat64 methods */
static asynStatus float64Write(void *drvPvt,asynUser *pasynUser,
    epicsFloat64 value);
static asynStatus float64Read(void *drvPvt,asynUser *pasynUser,
    epicsFloat64 *value);

static int int32DriverInit(const char *dn,int low,int high)
{
    drvPvt    *pdrvPvt;
    char       *portName;
    asynStatus status;
    size_t     nbytes;
    int        addr;
    asynInt32  *pasynInt32;
    asynFloat64 *pasynFloat64;

    nbytes = sizeof(drvPvt) + sizeof(asynInt32) + sizeof(asynFloat64);
    nbytes += strlen(dn) + 1;
    pdrvPvt = callocMustSucceed(nbytes,sizeof(char),"int32DriverInit");
    pasynInt32 = (asynInt32 *)(pdrvPvt + 1);
    pasynFloat64 = (asynFloat64 *)(pasynInt32 + 1);
    portName = (char *)(pasynFloat64 + 1);
    strcpy(portName,dn);
    pdrvPvt->portName = portName;
    pdrvPvt->lock = epicsMutexMustCreate();
    pdrvPvt->waitWork = epicsEventCreate(epicsEventEmpty);
    pdrvPvt->common.interfaceType = asynCommonType;
    pdrvPvt->common.pinterface  = (void *)&common;
    pdrvPvt->common.drvPvt = pdrvPvt;
    pdrvPvt->asynDrvUser.interfaceType = asynDrvUserType;
    pdrvPvt->asynDrvUser.pinterface = (void *)&drvUser;
    pdrvPvt->asynDrvUser.drvPvt = pdrvPvt;
    pdrvPvt->low = low;
    pdrvPvt->high = high;
    status = pasynManager->registerPort(portName,ASYN_MULTIDEVICE,1,0,0);
    if(status!=asynSuccess) {
        printf("int32DriverInit registerDriver failed\n");
        return 0;
    }
    status = pasynManager->registerInterface(portName,&pdrvPvt->common);
    if(status!=asynSuccess){
        printf("int32DriverInit registerInterface failed\n");
        return 0;
    }
    status = pasynManager->registerInterface(portName,&pdrvPvt->asynDrvUser);
    if(status!=asynSuccess){
        printf("int32DriverInit registerInterface failed\n");
        return 0;
    }
    pasynInt32->write = int32Write;
    pasynInt32->read = int32Read;
    pasynInt32->getBounds = int32GetBounds;
    pdrvPvt->asynInt32.interfaceType = asynInt32Type;
    pdrvPvt->asynInt32.pinterface  = pasynInt32;
    pdrvPvt->asynInt32.drvPvt = pdrvPvt;
    status = pasynInt32Base->initialize(pdrvPvt->portName, &pdrvPvt->asynInt32);
    if(status!=asynSuccess) return 0;
    pasynFloat64->write = float64Write;
    pasynFloat64->read = float64Read;
    pdrvPvt->asynFloat64.interfaceType = asynFloat64Type;
    pdrvPvt->asynFloat64.pinterface  = pasynFloat64;
    pdrvPvt->asynFloat64.drvPvt = pdrvPvt;
    status = pasynFloat64Base->initialize(pdrvPvt->portName,
        &pdrvPvt->asynFloat64);
    if(status!=asynSuccess) return 0;
    pdrvPvt->interruptDelay = 0.0;
    for(addr=0; addr<NCHANNELS; addr++) {
        pdrvPvt->channel[addr].value = pdrvPvt->low;
    }
    status = pasynManager->registerInterruptSource(portName,&pdrvPvt->asynInt32,
        &pdrvPvt->asynInt32Pvt);
    if(status!=asynSuccess) {
        printf("int32DriverInit registerInterruptSource failed\n");
    }
    status = pasynManager->registerInterruptSource(portName,&pdrvPvt->asynFloat64,
        &pdrvPvt->asynFloat64Pvt);
    if(status!=asynSuccess) {
        printf("int32DriverInit registerInterruptSource failed\n");
    }
    epicsThreadCreate("driverInt32",
        epicsThreadPriorityHigh,
        epicsThreadGetStackSize(epicsThreadStackSmall),
        (EPICSTHREADFUNC)interruptThread,pdrvPvt);
    return(0);
}

static asynStatus getAddr(drvPvt *pdrvPvt,asynUser *pasynUser,
    int *paddr,int portOK)
{
    asynStatus status;  

    status = pasynManager->getAddr(pasynUser,paddr);
    if(status!=asynSuccess) return status;
    if(*paddr>=-1 && *paddr<NCHANNELS) return asynSuccess;
    if(!portOK && *paddr>=0) return asynSuccess;
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
        "%s addr %d is illegal; Must be >= %d and < %d",
        pdrvPvt->portName,*paddr,
        (portOK ? -1 : 0),NCHANNELS);
    return asynError;
}
static void interruptThread(drvPvt *pdrvPvt)
{
    while(1) {
        epicsEventMustWait(pdrvPvt->waitWork);
        while(1) {
            int addr;
            epicsInt32 value;
            ELLLIST *pclientList;
            interruptNode *pnode;
            asynInt32Interrupt *pinterrupt;
    
            if(pdrvPvt->interruptDelay <= .0001) break;
            for(addr=0; addr<NCHANNELS; addr++) {
                chanPvt *pchannel = &pdrvPvt->channel[addr];
                epicsMutexMustLock(pdrvPvt->lock);
                value = pchannel->value;
                if(value>=pdrvPvt->high) {
                    value = pdrvPvt->low;
                } else {
                    value++;
                }
                pchannel->value = value;
                epicsMutexUnlock(pdrvPvt->lock);
            }
            pasynManager->interruptStart(pdrvPvt->asynInt32Pvt, &pclientList);
            pnode = (interruptNode *)ellFirst(pclientList);
            while (pnode) {
                pinterrupt = pnode->drvPvt;
                addr = pinterrupt->addr;
                pinterrupt->callback(pinterrupt->userPvt, pinterrupt->pasynUser,
                    pdrvPvt->channel[addr].value);
                pnode = (interruptNode *)ellNext(&pnode->node);
            }
            pasynManager->interruptEnd(pdrvPvt->asynInt32Pvt);
            epicsThreadSleep(pdrvPvt->interruptDelay);
        }
    }
}

/* asynCommon methods */
static void report(void *pvt,FILE *fp,int details)
{
    drvPvt *pdrvPvt = (drvPvt *)pvt;

    fprintf(fp,"    int32Driver: connected:%s interruptDelay = %f\n",
        (pdrvPvt->connected ? "Yes" : "No"),
        pdrvPvt->interruptDelay);
}

static asynStatus connect(void *pvt,asynUser *pasynUser)
{
    drvPvt   *pdrvPvt = (drvPvt *)pvt;
    int        addr;
    asynStatus status;  

    status = getAddr(pdrvPvt,pasynUser,&addr,1);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s int32Driver:connect addr %d\n",pdrvPvt->portName,addr);
    if(addr>=0) {
        pasynManager->exceptionConnect(pasynUser);
        return asynSuccess;
    }
    if(pdrvPvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
           "%s int32Driver:connect port already connected\n",
           pdrvPvt->portName);
        return asynError;
    }
    pdrvPvt->connected = 1;
    pasynManager->exceptionConnect(pasynUser);
    return asynSuccess;
}

static asynStatus disconnect(void *pvt,asynUser *pasynUser)
{
    drvPvt    *pdrvPvt = (drvPvt *)pvt;
    int        addr;
    asynStatus status;  

    status = getAddr(pdrvPvt,pasynUser,&addr,1);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s int32Driver:disconnect addr %d\n",pdrvPvt->portName,addr);
    if(addr>=0) {
        pasynManager->exceptionDisconnect(pasynUser);
        return asynSuccess;
    }
    if(!pdrvPvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
           "%s int32Driver:disconnect port not connected\n",
           pdrvPvt->portName);
        return asynError;
    }
    pdrvPvt->connected = 0;
    pasynManager->exceptionDisconnect(pasynUser);
    return asynSuccess;
}

static asynStatus int32Write(void *pvt,asynUser *pasynUser,
                                 epicsInt32 value)
{
    drvPvt   *pdrvPvt = (drvPvt *)pvt;
    int        addr;
    asynStatus status;  
    ELLLIST *pclientList;
    interruptNode *pnode;
    asynInt32Interrupt *pinterrupt;

    status = getAddr(pdrvPvt,pasynUser,&addr,0);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s int32Driver:writeInt32 value %d\n",pdrvPvt->portName,value);
    if(!pdrvPvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s int32Driver:read not connected\n",pdrvPvt->portName);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s int32Driver:read not connected",pdrvPvt->portName);
        return asynError;
    }
    epicsMutexMustLock(pdrvPvt->lock);
    pdrvPvt->channel[addr].value = value;
    epicsMutexUnlock(pdrvPvt->lock);
    asynPrint(pasynUser,ASYN_TRACEIO_DRIVER,
        "%s addr %d write %d\n",pdrvPvt->portName,addr,value);
    pasynManager->interruptStart(pdrvPvt->asynInt32Pvt, &pclientList);
    pnode = (interruptNode *)ellFirst(pclientList);
    while (pnode) {
        pinterrupt = pnode->drvPvt;
        if(addr==pinterrupt->addr) {
            pinterrupt->callback(pinterrupt->userPvt, pinterrupt->pasynUser,
                pdrvPvt->channel[addr].value);
        }
        pnode = (interruptNode *)ellNext(&pnode->node);
    }
    pasynManager->interruptEnd(pdrvPvt->asynInt32Pvt);
    return asynSuccess;
}

static asynStatus int32Read(void *pvt,asynUser *pasynUser,
                                 epicsInt32 *value)
{
    drvPvt *pdrvPvt = (drvPvt *)pvt;
    int        addr;
    asynStatus status;  

    status = getAddr(pdrvPvt,pasynUser,&addr,0);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s int32Driver:readInt32 value %p\n",pdrvPvt->portName,value);
    if(!pdrvPvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s int32Driver:read  not connected\n",pdrvPvt->portName);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s int32Driver:read not connected",pdrvPvt->portName);
        return asynError;
    }
    epicsMutexMustLock(pdrvPvt->lock);
    *value = pdrvPvt->channel[addr].value;
    epicsMutexUnlock(pdrvPvt->lock);
    asynPrint(pasynUser,ASYN_TRACEIO_DRIVER,
        "%s read %d\n",pdrvPvt->portName,*value);
    return asynSuccess;
}
static asynStatus int32GetBounds(void *pvt, asynUser *pasynUser,
                                epicsInt32 *low, epicsInt32 *high)
{
    drvPvt *pdrvPvt = (drvPvt *)pvt;

    *low = pdrvPvt->low; *high = pdrvPvt->high;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s int32GetBounds low %d high %d\n",pdrvPvt->portName,*low,*high);
    return asynSuccess;
}

static asynStatus float64Write(void *pvt,asynUser *pasynUser,
                              epicsFloat64 value)
{
    drvPvt   *pdrvPvt = (drvPvt *)pvt;
    int        addr;
    asynStatus status;
    ELLLIST *pclientList;
    interruptNode *pnode;
    asynFloat64Interrupt *pinterrupt;

    status = getAddr(pdrvPvt,pasynUser,&addr,0);
    if(status!=asynSuccess) return status;
    epicsMutexMustLock(pdrvPvt->lock);
    pdrvPvt->interruptDelay = value;
    epicsMutexUnlock(pdrvPvt->lock);
    epicsEventSignal(pdrvPvt->waitWork);
    asynPrint(pasynUser,ASYN_TRACEIO_DRIVER,
        "%s addr %d write %f\n",pdrvPvt->portName,addr,value);
    pasynManager->interruptStart(pdrvPvt->asynFloat64Pvt, &pclientList);
    pnode = (interruptNode *)ellFirst(pclientList);
    while (pnode) {
        pinterrupt = pnode->drvPvt;
        if(addr==pinterrupt->addr && pinterrupt->pasynUser->reason==1) {
            pinterrupt->callback(pinterrupt->userPvt,pinterrupt->pasynUser,value);
            break;
        }
        pnode = (interruptNode *)ellNext(&pnode->node);
    }
    pasynManager->interruptEnd(pdrvPvt->asynFloat64Pvt);
    return asynSuccess;
}

static asynStatus float64Read(void *pvt,asynUser *pasynUser,
                              epicsFloat64 *value)
{
    drvPvt *pdrvPvt = (drvPvt *)pvt;
    int        addr;
    asynStatus status;

    status = getAddr(pdrvPvt,pasynUser,&addr,0);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s %d uint32DigitalDriver:float64Read\n",pdrvPvt->portName,addr);
    if(!pdrvPvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s uint32DigitalDriver:read  not connected\n",pdrvPvt->portName);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s uint32DigitalDriver:read not connected",pdrvPvt->portName);
        return asynError;
    }
    epicsMutexMustLock(pdrvPvt->lock);
    *value = pdrvPvt->interruptDelay;
    epicsMutexUnlock(pdrvPvt->lock);
    asynPrint(pasynUser,ASYN_TRACEIO_DRIVER,
        "%s read %f\n",pdrvPvt->portName,*value);
    return asynSuccess;
}

static const char *testDriverReason = "testDriverReason";
static const char *skipWhite(const char *pstart)
{
    const char *p = pstart;
    while(*p && isspace((int)*p)) p++;
    return p;
}

static asynStatus create(void *drvPvt,asynUser *pasynUser,
    const char *drvInfo, const char **pptypeName,size_t *psize)
{
    const char *pnext;
    long  reason = 0;

    if(!drvInfo) {
        reason = 0;
    } else {
        char *endp;

        pnext = skipWhite(drvInfo);
        if(strlen(pnext)==0) {
            reason = 0;
        } else {
            pnext = strstr(pnext,"reason");
            if(!pnext) goto error;
            pnext += strlen("reason");
            pnext = skipWhite(pnext);
            if(*pnext!='(') goto error;
            pnext++;
            pnext = skipWhite(pnext);
            errno = 0;
            reason = strtol(pnext,&endp,0);
            if(errno) {
                printf("strtol failed %s\n",strerror(errno));
                goto error;
            }
        }
    }
    pasynUser->reason = reason;
    if(pptypeName) *pptypeName = testDriverReason;
    if(psize) *psize = sizeof(int);
    return asynSuccess;
error:
    printf("asynDrvUser failed. got |%s| expecting reason(<int>)\n",drvInfo);
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
        "asynDrvUser failed. got |%s| expecting reason(<int>)",drvInfo);
    return asynError;
}

static asynStatus getType(void *drvPvt,asynUser *pasynUser,
    const char **pptypeName,size_t *psize)
{
    *pptypeName = testDriverReason;
    *psize = sizeof(int);
    return asynSuccess;
}

static asynStatus destroy(void *drvPvt,asynUser *pasynUser)
{ return asynSuccess;}

/* register int32DriverInit*/
static const iocshArg int32DriverInitArg0 = { "portName", iocshArgString };
static const iocshArg int32DriverInitArg1 = { "low", iocshArgInt };
static const iocshArg int32DriverInitArg2 = { "high", iocshArgInt };
static const iocshArg *int32DriverInitArgs[] = {
    &int32DriverInitArg0,&int32DriverInitArg1,&int32DriverInitArg2};
static const iocshFuncDef int32DriverInitFuncDef = {
    "int32DriverInit", 3, int32DriverInitArgs};
static void int32DriverInitCallFunc(const iocshArgBuf *args)
{
    int32DriverInit(args[0].sval,args[1].ival,args[2].ival);
}

static void int32DriverRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&int32DriverInitFuncDef, int32DriverInitCallFunc);
    }
}
epicsExportRegistrar(int32DriverRegister);
