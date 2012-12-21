/*uint32DigitalDriver.c */
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
#include <epicsEvent.h>
#include <epicsMutex.h>
#include <epicsThread.h>
#include <iocsh.h>

#include <asynDriver.h>
#include <asynDrvUser.h>
#include <asynUInt32Digital.h>
#include <asynFloat64.h>

#include <epicsExport.h>

#define NCHANNELS 4

typedef struct chanPvt {
    epicsUInt32 value;
    void       *asynPvt;
}chanPvt;

typedef struct drvPvt {
    const char    *portName;
    epicsMutexId  lock;
    epicsEventId  waitWork;
    int           connected;
    epicsFloat64  interruptDelay;
    asynInterface common;
    asynInterface asynDrvUser;
    asynInterface asynUInt32Digital;
    asynInterface asynFloat64;
    chanPvt       channel[NCHANNELS];
    void          *asynUInt32DigitalPvt; /* For registerInterruptSource*/
    void          *asynFloat64Pvt; /* For registerInterruptSource*/
}drvPvt;

static int uint32DigitalDriverInit(const char *dn);
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

/* asynUInt32Digital methods */
static asynStatus uint32Write(void *drvPvt,asynUser *pasynUser,
                              epicsUInt32 value,epicsUInt32 mask);
static asynStatus uint32Read(void *drvPvt,asynUser *pasynUser,
                              epicsUInt32 *value,epicsUInt32 mask);

/* asynFloat64 methods */
static asynStatus float64Write(void *drvPvt,asynUser *pasynUser, epicsFloat64 value);
static asynStatus float64Read(void *drvPvt,asynUser *pasynUser, epicsFloat64 *value);

static int uint32DigitalDriverInit(const char *dn)
{
    drvPvt    *pdrvPvt;
    char       *portName;
    asynStatus status;
    size_t     nbytes;
    int        addr;
    asynUInt32Digital *pasynUInt32Digital;
    asynFloat64       *pasynFloat64;

    nbytes = sizeof(drvPvt) + sizeof(asynUInt32Digital) + sizeof(asynFloat64);
    nbytes += strlen(dn) + 1;
    pdrvPvt = callocMustSucceed(nbytes,sizeof(char),"uint32DigitalDriverInit");
    pasynUInt32Digital = (asynUInt32Digital *)(pdrvPvt + 1);
    pasynFloat64 = (asynFloat64 *)(pasynUInt32Digital + 1);
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
    status = pasynManager->registerPort(portName,ASYN_MULTIDEVICE,1,0,0);
    if(status!=asynSuccess) {
        printf("uint32DigitalDriverInit registerDriver failed\n");
        return 0;
    }
    status = pasynManager->registerInterface(portName,&pdrvPvt->common);
    if(status!=asynSuccess){
        printf("uint32DigitalDriverInit registerInterface failed\n");
        return 0;
    }
    status = pasynManager->registerInterface(portName,&pdrvPvt->asynDrvUser);
    if(status!=asynSuccess){
        printf("uint32DigitalDriverInit registerInterface failed\n");
        return 0;
    }
    pasynUInt32Digital->write = uint32Write;
    pasynUInt32Digital->read = uint32Read;
    pdrvPvt->asynUInt32Digital.interfaceType = asynUInt32DigitalType;
    pdrvPvt->asynUInt32Digital.pinterface  = pasynUInt32Digital;
    pdrvPvt->asynUInt32Digital.drvPvt = pdrvPvt;
    status = pasynUInt32DigitalBase->initialize(pdrvPvt->portName,
        &pdrvPvt->asynUInt32Digital);
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
        pdrvPvt->channel[addr].value = 0;
    }
    status = pasynManager->registerInterruptSource(
        portName,&pdrvPvt->asynUInt32Digital,&pdrvPvt->asynUInt32DigitalPvt);
    if(status!=asynSuccess) {
        printf("uint32DigitalDriverInit registerInterruptSource failed\n");
    }
    status = pasynManager->registerInterruptSource(
        portName,&pdrvPvt->asynFloat64, &pdrvPvt->asynFloat64Pvt);
    if(status!=asynSuccess) {
        printf("uint32DigitalDriverInit registerInterruptSource failed\n");
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
            epicsUInt32 value;
            ELLLIST *pclientList;
            interruptNode *pnode;
            asynUInt32DigitalInterrupt *pinterrupt;
    
            if(pdrvPvt->interruptDelay <= .0001) break;
            for(addr=0; addr<NCHANNELS; addr++) {
                chanPvt *pchannel = &pdrvPvt->channel[addr];
                epicsMutexMustLock(pdrvPvt->lock);
                value = pchannel->value;
                if(value<0xf) {
                    value +=1;
                } else if(value&0x80000000) {
                    value = 0;
                } else {
                    value <<= 1;
                }
                pchannel->value = value;
                epicsMutexUnlock(pdrvPvt->lock);
            }
            pasynManager->interruptStart(
                pdrvPvt->asynUInt32DigitalPvt,&pclientList);
            pnode = (interruptNode *)ellFirst(pclientList);
            while (pnode) {
                pinterrupt = pnode->drvPvt;
                addr = pinterrupt->addr;
                pinterrupt->callback(pinterrupt->userPvt, pinterrupt->pasynUser,
                    pdrvPvt->channel[addr].value);
                pnode = (interruptNode *)ellNext(&pnode->node);
            }
            pasynManager->interruptEnd(pdrvPvt->asynUInt32DigitalPvt);
            epicsThreadSleep(pdrvPvt->interruptDelay);
        }
    }
}


/* asynCommon methods */
static void report(void *pvt,FILE *fp,int details)
{
    drvPvt *pdrvPvt = (drvPvt *)pvt;

    fprintf(fp,"    uint32DigitalDriver: connected:%s interruptDelay = %f\n",
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
        "%s uint32DigitalDriver:connect addr %d\n",pdrvPvt->portName,addr);
    if(addr>=0) {
        pasynManager->exceptionConnect(pasynUser);
        return asynSuccess;
    }
    if(pdrvPvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
           "%s uint32DigitalDriver:connect port already connected\n",
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
        "%s uint32DigitalDriver:disconnect addr %d\n",pdrvPvt->portName,addr);
    if(addr>=0) {
        pasynManager->exceptionDisconnect(pasynUser);
        return asynSuccess;
    }
    if(!pdrvPvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
           "%s uint32DigitalDriver:disconnect port not connected\n",
           pdrvPvt->portName);
        return asynError;
    }
    pdrvPvt->connected = 0;
    pasynManager->exceptionDisconnect(pasynUser);
    return asynSuccess;
}

static asynStatus uint32Write(void *pvt,asynUser *pasynUser,
                            epicsUInt32 value,epicsUInt32 mask)
{
    drvPvt   *pdrvPvt = (drvPvt *)pvt;
    int        addr;
    asynStatus status;  
    epicsUInt32 newvalue;
    ELLLIST *pclientList;
    interruptNode *pnode;
    asynUInt32DigitalInterrupt *pinterrupt;

    status = getAddr(pdrvPvt,pasynUser,&addr,0);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s uint32DigitalDriver:writeInt32 value %d\n",pdrvPvt->portName,value);
    if(!pdrvPvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s uint32DigitalDriver:read not connected\n",pdrvPvt->portName);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s uint32DigitalDriver:read not connected",pdrvPvt->portName);
        return asynError;
    }
    epicsMutexMustLock(pdrvPvt->lock);
    newvalue = pdrvPvt->channel[addr].value;
    newvalue &= ~mask;
    newvalue |= value&mask;
    pdrvPvt->channel[addr].value = newvalue;
    epicsMutexUnlock(pdrvPvt->lock);
    asynPrint(pasynUser,ASYN_TRACEIO_DRIVER,
        "%s addr %d write %d\n",pdrvPvt->portName,addr,value);
    pasynManager->interruptStart(pdrvPvt->asynUInt32DigitalPvt, &pclientList);
    pnode = (interruptNode *)ellFirst(pclientList);
    while (pnode) {
        pinterrupt = pnode->drvPvt;
        if(addr==pinterrupt->addr) {
            pinterrupt->callback(pinterrupt->userPvt, pinterrupt->pasynUser,
                pdrvPvt->channel[addr].value);
        }
        pnode = (interruptNode *)ellNext(&pnode->node);
    }
    pasynManager->interruptEnd(pdrvPvt->asynUInt32DigitalPvt);
    return asynSuccess;
}

static asynStatus uint32Read(void *pvt,asynUser *pasynUser,
                            epicsUInt32 *value,epicsUInt32 mask)
{
    drvPvt *pdrvPvt = (drvPvt *)pvt;
    int        addr;
    asynStatus status;  

    status = getAddr(pdrvPvt,pasynUser,&addr,0);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s %d uint32DigitalDriver:readInt32\n",pdrvPvt->portName,addr);
    if(!pdrvPvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s uint32DigitalDriver:read  not connected\n",pdrvPvt->portName);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s uint32DigitalDriver:read not connected",pdrvPvt->portName);
        return asynError;
    }
    epicsMutexMustLock(pdrvPvt->lock);
    *value = mask & pdrvPvt->channel[addr].value;
    epicsMutexUnlock(pdrvPvt->lock);
    asynPrint(pasynUser,ASYN_TRACEIO_DRIVER,
        "%s read %d\n",pdrvPvt->portName,*value);
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


/* register uint32DigitalDriverInit*/
static const iocshArg uint32DigitalDriverInitArg0 = { "portName", iocshArgString };
static const iocshArg *uint32DigitalDriverInitArgs[] = {
    &uint32DigitalDriverInitArg0};
static const iocshFuncDef uint32DigitalDriverInitFuncDef = {
    "uint32DigitalDriverInit", 1, uint32DigitalDriverInitArgs};
static void uint32DigitalDriverInitCallFunc(const iocshArgBuf *args)
{
    uint32DigitalDriverInit(args[0].sval);
}

static void uint32DigitalDriverRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&uint32DigitalDriverInitFuncDef, uint32DigitalDriverInitCallFunc);
    }
}
epicsExportRegistrar(uint32DigitalDriverRegister);
