/*  asynFloat64Base.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*  11-OCT-2004 Marty Kraimer
*/

#include <asynDriver.h>
#include <epicsTypes.h>

#define epicsExportSharedSymbols

#include "asynFloat64.h"

typedef struct pvt {
    interruptCallbackFloat64 callback;
    void                  *userPvt;
    epicsFloat64           prevValue;
} pvt;

static void float64Callback(void *userPvt, void *pvalue);

static asynStatus initialize(const char *portName, asynInterface *pfloat64Interface);

static asynFloat64Base float64Base = {initialize};
epicsShareDef asynFloat64Base *pasynFloat64Base = &float64Base;

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser,
                               epicsFloat64 value);
static asynStatus readDefault(void *drvPvt, asynUser *pasynUser,
                              epicsFloat64 *value);
static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser,
       interruptCallbackFloat64 callback, void *userPvt, void **registrarPvt);
static asynStatus cancelInterruptUser(void *registrarPvt, asynUser *pasynUser);

static void float64Callback(void *userPvt, void *pvalue)
{
    pvt        *ppvt = (pvt *)userPvt;
    epicsFloat64 value = *(epicsFloat64 *)pvalue;
    epicsFloat64 prevValue = ppvt->prevValue;

    if(value!=prevValue) {
        ppvt->callback(ppvt->userPvt,value);
        ppvt->prevValue = value;
    }
}

static asynStatus initialize(const char *portName,
    asynInterface *pfloat64Interface)
{
    asynFloat64 *pasynFloat64 = (asynFloat64 *)pfloat64Interface->pinterface;

    if(!pasynFloat64->write) pasynFloat64->write = writeDefault;
    if(!pasynFloat64->read) pasynFloat64->read = readDefault;
    if(!pasynFloat64->registerInterruptUser)
        pasynFloat64->registerInterruptUser = registerInterruptUser;
    if(!pasynFloat64->cancelInterruptUser)
        pasynFloat64->cancelInterruptUser = cancelInterruptUser;
    return pasynManager->registerInterface(portName,pfloat64Interface);
}

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser,
    epicsFloat64 value)
{
    const char *portName;
    asynStatus status;
    int        addr;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
        "write is not supported\n");
    asynPrint(pasynUser,ASYN_TRACE_ERROR,
        "%s %d write is not supported\n",portName,addr);
    return asynError;
}

static asynStatus readDefault(void *drvPvt, asynUser *pasynUser,
    epicsFloat64 *value)
{
    const char *portName;
    asynStatus status;
    int        addr;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
        "write is not supported\n");
    asynPrint(pasynUser,ASYN_TRACE_ERROR,
        "%s %d read is not supported\n",portName,addr);
    return asynError;
}
    
static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser,
      interruptCallbackFloat64 callback, void *userPvt, void **registrarPvt)
{
    pvt *ppvt = pasynManager->memMalloc(sizeof(pvt));
    const char *portName;
    asynStatus status;
    int        addr;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    ppvt->callback = callback;
    ppvt->userPvt = userPvt;
    ppvt->prevValue = 0.0;
    *registrarPvt = ppvt;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d registerInterruptUser\n",portName,addr);
    return pasynManager->registerInterruptUser(pasynUser,float64Callback,ppvt);
}

static asynStatus cancelInterruptUser(void *registrarPvt, asynUser *pasynUser)
{
    pvt *ppvt = (pvt *)registrarPvt;
    asynStatus status;
    const char *portName;
    int        addr;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d cancelInterruptUser\n",portName,addr);
    status = pasynManager->cancelInterruptUser(pasynUser);
    pasynManager->memFree(ppvt,sizeof(pvt));
    return status;
}
