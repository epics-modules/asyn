/*  asynInt32Base.c */
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

#include "asynInt32.h"

typedef struct pvt {
    interruptCallbackInt32 callback;
    void       *userPvt;
    epicsInt32 prevValue;
} pvt;

static void int32Callback(void *userPvt, void *pvalue);

static asynStatus initialize(const char *portName, asynInterface *pint32Interface);

static asynInt32Base int32Base = {initialize};
epicsShareDef asynInt32Base *pasynInt32Base = &int32Base;

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser,
                              epicsInt32 value);
static asynStatus readDefault(void *drvPvt, asynUser *pasynUser,
                              epicsInt32 *value);
static asynStatus getBounds(void *drvPvt, asynUser *pasynUser, 
                            epicsInt32 *low, epicsInt32 *high);
static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser,
                               interruptCallbackInt32 callback, void *userPvt,
                               void **registrarPvt);
static asynStatus cancelInterruptUser(void *registrarPvt, asynUser *pasynUser);

static void int32Callback(void *userPvt, void *pvalue)
{
    pvt        *ppvt = (pvt *)userPvt;
    epicsInt32 value = *(epicsInt32 *)pvalue;
    epicsInt32 prevValue = ppvt->prevValue;

    if(value!=prevValue) {
        ppvt->callback(ppvt->userPvt,value);
        ppvt->prevValue = value;
    }
}

asynStatus initialize(const char *portName, asynInterface *pint32Interface)
{
    asynInt32 *pasynInt32 = (asynInt32 *)pint32Interface->pinterface;

    if(!pasynInt32->write) pasynInt32->write = writeDefault;
    if(!pasynInt32->read) pasynInt32->read = readDefault;
    if(!pasynInt32->getBounds) pasynInt32->getBounds = getBounds;
    if(!pasynInt32->registerInterruptUser)
        pasynInt32->registerInterruptUser = registerInterruptUser;
    if(!pasynInt32->cancelInterruptUser) 
        pasynInt32->cancelInterruptUser = cancelInterruptUser;
    return pasynManager->registerInterface(portName,pint32Interface);
}

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser,
    epicsInt32 value)
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
    epicsInt32 *value)
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

static asynStatus getBounds(void *drvPvt, asynUser *pasynUser, 
                            epicsInt32 *low, epicsInt32 *high)
{
    const char *portName;
    asynStatus status;
    int        addr;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    *low = *high = 0;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d getBounds setting low=high=0\n",portName,addr);
    return asynSuccess;
}
    
static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser,
                               interruptCallbackInt32 callback, void *userPvt,
                               void **registrarPvt)
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
    return pasynManager->registerInterruptUser(pasynUser,int32Callback,ppvt);
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
