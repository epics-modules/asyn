/*  asynUInt32DigitalBase.c */
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

#include "asynUInt32Digital.h"

typedef struct pvt {
    interruptCallbackUInt32Digital callback;
    void                  *userPvt;
    epicsUInt32           mask;
    epicsUInt32           prevValue;
} pvt;

static void uint32Callback(void *userPvt, void *pvalue);
static asynStatus initialize(const char *portName, asynInterface *puint32Interface);
static asynUInt32DigitalBase uint32Base = {initialize};
epicsShareDef asynUInt32DigitalBase *pasynUInt32DigitalBase = &uint32Base;

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser,
                               epicsUInt32 value, epicsUInt32 mask);
static asynStatus readDefault(void *drvPvt, asynUser *pasynUser,
                              epicsUInt32 *value, epicsUInt32 mask);
static asynStatus setInterrupt(void *drvPvt, asynUser *pasynUser,
                               epicsUInt32 mask, interruptReason reason);
static asynStatus clearInterrupt(void *drvPvt, asynUser *pasynUser,
                               epicsUInt32 mask);
static asynStatus getInterrupt(void *drvPvt, asynUser *pasynUser,
                               epicsUInt32 *mask, interruptReason reason);
static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser,
       interruptCallbackUInt32Digital callback, void *userPvt,epicsUInt32 mask,
       void **registrarPvt);
static asynStatus cancelInterruptUser(void *registrarPvt, asynUser *pasynUser);

static void uint32Callback(void *userPvt, void *pvalue)
{
    pvt        *ppvt = (pvt *)userPvt;
    epicsUInt32 mask = ppvt->mask;
    epicsUInt32 value = mask&(*(epicsUInt32 *)pvalue);
    epicsUInt32 prevValue = mask&ppvt->prevValue;

    if(value^prevValue) {
        ppvt->callback(ppvt->userPvt,value);
        ppvt->prevValue = value;
    }
}

static asynStatus initialize(const char *portName,
    asynInterface *puint32Interface)
{
    asynUInt32Digital *pasynUInt32Digital =
        (asynUInt32Digital *)puint32Interface->pinterface;

    if(!pasynUInt32Digital->write) pasynUInt32Digital->write = writeDefault;
    if(!pasynUInt32Digital->read) pasynUInt32Digital->read = readDefault;
    if(!pasynUInt32Digital->setInterrupt)
        pasynUInt32Digital->setInterrupt = setInterrupt;
    if(!pasynUInt32Digital->clearInterrupt)
        pasynUInt32Digital->clearInterrupt = clearInterrupt;
    if(!pasynUInt32Digital->getInterrupt)
        pasynUInt32Digital->getInterrupt = getInterrupt;
    if(!pasynUInt32Digital->registerInterruptUser)
        pasynUInt32Digital->registerInterruptUser = registerInterruptUser;
    if(!pasynUInt32Digital->cancelInterruptUser)
        pasynUInt32Digital->cancelInterruptUser = cancelInterruptUser;
    return pasynManager->registerInterface(portName,puint32Interface);
}

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser,
    epicsUInt32 value,epicsUInt32 mask)
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
    epicsUInt32 *value, epicsUInt32 mask)
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
static asynStatus setInterrupt(void *drvPvt, asynUser *pasynUser,
                               epicsUInt32 mask, interruptReason reason)
{
    const char *portName;
    asynStatus status;
    int        addr;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
        "setInterrupt is not supported\n");
    asynPrint(pasynUser,ASYN_TRACE_ERROR,
        "%s %d setInterrupt is not supported\n",portName,addr);
    return asynError;
}

static asynStatus clearInterrupt(void *drvPvt, asynUser *pasynUser,
                               epicsUInt32 mask)
{
    const char *portName;
    asynStatus status;
    int        addr;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
        "clearInterrupt is not supported\n");
    asynPrint(pasynUser,ASYN_TRACE_ERROR,
        "%s %d clearInterrupt is not supported\n",portName,addr);
    return asynError;
}

static asynStatus getInterrupt(void *drvPvt, asynUser *pasynUser,
                               epicsUInt32 *mask, interruptReason reason)
{
    const char *portName;
    asynStatus status;
    int        addr;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
        "getInterrupt is not supported\n");
    asynPrint(pasynUser,ASYN_TRACE_ERROR,
        "%s %d getInterrupt is not supported\n",portName,addr);
    return asynError;
}


static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser,
      interruptCallbackUInt32Digital callback, void *userPvt,epicsUInt32 mask,
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
    ppvt->mask = mask;
    ppvt->prevValue = 0;
    *registrarPvt = ppvt;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d registerInterruptUser\n",portName,addr);
    return pasynManager->registerInterruptUser(pasynUser,uint32Callback,ppvt);
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

