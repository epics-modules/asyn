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

#include <cantProceed.h>
#include <epicsStdio.h>
#include <epicsMutex.h>
#include <epicsThread.h>
#include <epicsExport.h>
#include <iocsh.h>

#include <asynDriver.h>
#include <asynDrvUser.h>
#include <asynInt32.h>

#define NCHANNELS 16

typedef struct chanPvt {
    epicsInt32 value;
    void       *asynPvt;
}chanPvt;

typedef struct drvPvt {
    const char    *portName;
    epicsMutexId  lock;
    int           connected;
    double        interruptDelay;
    asynInterface common;
    asynInterface asynInt32;
    epicsInt32    low;
    epicsInt32    high;
    chanPvt       channel[NCHANNELS];
}drvPvt;

static int int32DriverInit(const char *dn, double interruptDelay,int low,int high);
static asynStatus getAddr(drvPvt *pdrvPvt,asynUser *pasynUser,
    int *paddr,int portOK);
static void interruptThread(drvPvt *pdrvPvt);

/* asynCommon methods */
static void report(void *drvPvt,FILE *fp,int details);
static asynStatus connect(void *drvPvt,asynUser *pasynUser);
static asynStatus disconnect(void *drvPvt,asynUser *pasynUser);
static asynCommon common = { report, connect, disconnect };

/* asynInt32 methods */
static asynStatus int32Write(void *drvPvt,asynUser *pasynUser,epicsInt32 value);
static asynStatus int32Read(void *drvPvt,asynUser *pasynUser,epicsInt32 *value);
static asynStatus int32GetBounds(void *drvPvt, asynUser *pasynUser,
                                epicsInt32 *low, epicsInt32 *high);
/*use default for registerCallback,cancelCallback*/
static asynInt32 int32Cache = { int32Write, int32Read, int32GetBounds, 0, 0 };
/*use default for write,registerCallback,cancelCallback*/
static asynInt32 int32Interrupt = { 0, int32Read, int32GetBounds, 0, 0 };

static int int32DriverInit(const char *dn, double interruptDelay,int low,int high)
{
    drvPvt    *pdrvPvt;
    char       *portName;
    asynStatus status;
    int        nbytes;
    int        addr;

    nbytes = sizeof(drvPvt) + strlen(dn) + 1;
    pdrvPvt = callocMustSucceed(nbytes,sizeof(char),"int32DriverInit");
    portName = (char *)(pdrvPvt + 1);
    strcpy(portName,dn);
    pdrvPvt->portName = portName;
    pdrvPvt->lock = epicsMutexMustCreate();
    pdrvPvt->common.interfaceType = asynCommonType;
    pdrvPvt->common.pinterface  = (void *)&common;
    pdrvPvt->common.drvPvt = pdrvPvt;
    pdrvPvt->asynInt32.interfaceType = asynInt32Type;
    pdrvPvt->asynInt32.pinterface  =
        (interruptDelay>0.0) ? (void *)&int32Interrupt : (void *)&int32Cache;
    pdrvPvt->asynInt32.drvPvt = pdrvPvt;
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
    status = pasynInt32Base->initialize(pdrvPvt->portName, &pdrvPvt->asynInt32);
    if(status!=asynSuccess) return 0;
    pdrvPvt->interruptDelay = interruptDelay;
    for(addr=0; addr<NCHANNELS; addr++) {
        pdrvPvt->channel[addr].value = pdrvPvt->low;
        status = pasynManager->registerInterruptSource(portName,addr,
            pdrvPvt,&pdrvPvt->channel[addr].asynPvt);
        if(status!=asynSuccess) {
            printf("int32DriverInit registerCallbackDriver failed\n");
        }
    }
    if(interruptDelay>0.0) {
        epicsThreadCreate("driverInt32",
            epicsThreadPriorityHigh,
            epicsThreadGetStackSize(epicsThreadStackSmall),
            (EPICSTHREADFUNC)interruptThread,pdrvPvt);
    }
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
        "%s addr %d is illegal; Must be >= %d and < %d\n",
        pdrvPvt->portName,*paddr,
        (portOK ? -1 : 0),NCHANNELS);
    return asynError;
}
static void interruptThread(drvPvt *pdrvPvt)
{
    while(1) {
        int addr;
        epicsInt32 value;

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
            pasynManager->interrupt(pdrvPvt->channel[addr].asynPvt,
                addr,0,&value);
        }
        epicsThreadSleep(pdrvPvt->interruptDelay);
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

    status = getAddr(pdrvPvt,pasynUser,&addr,0);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s int32Driver:writeInt32 value %d\n",pdrvPvt->portName,value);
    if(!pdrvPvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s int32Driver:read not connected\n",pdrvPvt->portName);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s int32Driver:read not connected\n",pdrvPvt->portName);
        return asynError;
    }
    epicsMutexMustLock(pdrvPvt->lock);
    pdrvPvt->channel[addr].value = value;
    epicsMutexUnlock(pdrvPvt->lock);
    asynPrint(pasynUser,ASYN_TRACEIO_DRIVER,
        "%s addr %d write %d\n",pdrvPvt->portName,addr,value);
    pasynManager->interrupt(pdrvPvt->channel[addr].asynPvt,addr,0,&value);
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
        "%s int32Driver:readInt32 value %d\n",pdrvPvt->portName,value);
    if(!pdrvPvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s int32Driver:read  not connected\n",pdrvPvt->portName);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s int32Driver:read not connected\n",pdrvPvt->portName);
        return asynError;
    }
    epicsMutexMustLock(pdrvPvt->lock);
    *value = pdrvPvt->channel[addr].value;
    epicsMutexUnlock(pdrvPvt->lock);
    asynPrint(pasynUser,ASYN_TRACEIO_DRIVER,
        "%s read %d\n",pdrvPvt->portName,value);
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

/* register int32DriverInit*/
static const iocshArg int32DriverInitArg0 = { "portName", iocshArgString };
static const iocshArg int32DriverInitArg1 = { "interruptDelay", iocshArgDouble };
static const iocshArg int32DriverInitArg2 = { "low", iocshArgInt };
static const iocshArg int32DriverInitArg3 = { "high", iocshArgInt };
static const iocshArg *int32DriverInitArgs[] = {
    &int32DriverInitArg0,&int32DriverInitArg1,
    &int32DriverInitArg2,&int32DriverInitArg3};
static const iocshFuncDef int32DriverInitFuncDef = {
    "int32DriverInit", 4, int32DriverInitArgs};
static void int32DriverInitCallFunc(const iocshArgBuf *args)
{
    int32DriverInit(args[0].sval,args[1].dval,args[2].ival,args[3].ival);
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
