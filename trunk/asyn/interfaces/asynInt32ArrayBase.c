/*  asynInt32ArrayBase.c */
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

#include "asynInt32Array.h"

static asynStatus initialize(const char *portName, asynInterface *pint32ArrayInterface);

static asynInt32ArrayBase int32ArrayBase = {initialize};
epicsShareDef asynInt32ArrayBase *pasynInt32ArrayBase = &int32ArrayBase;

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser,
                               epicsInt32 *value, size_t nelem);
static asynStatus readDefault(void *drvPvt, asynUser *pasynUser,
                              epicsInt32 *value, size_t nelem, size_t *nIn);
static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser,
                               interruptCallbackInt32Array callback, void *userPvt,
                               void **registrarPvt);
static asynStatus cancelInterruptUser(void *registrarPvt, asynUser *pasynUser);


asynStatus initialize(const char *portName, asynInterface *pint32ArrayInterface)
{
    asynInt32Array *pasynInt32Array = (asynInt32Array *)pint32ArrayInterface->pinterface;

    if(!pasynInt32Array->write) pasynInt32Array->write = writeDefault;
    if(!pasynInt32Array->read) pasynInt32Array->read = readDefault;
    if(!pasynInt32Array->registerInterruptUser)
        pasynInt32Array->registerInterruptUser = registerInterruptUser;
    if(!pasynInt32Array->cancelInterruptUser) 
        pasynInt32Array->cancelInterruptUser = cancelInterruptUser;
    return pasynManager->registerInterface(portName,pint32ArrayInterface);
}

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser,
    epicsInt32 *value, size_t nelem)
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
    epicsInt32 *value, size_t nelem, size_t *nIn)
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
                               interruptCallbackInt32Array callback, void *userPvt,
                               void **registrarPvt)
{
    const char *portName;
    asynStatus status;
    int        addr;
    interruptNode *pinterruptNode;
    asynInt32ArrayInterrupt *pasynInt32ArrayInterrupt;
    void *pinterruptPvt;

    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    status = pasynManager->getInterruptPvt(portName, asynInt32ArrayType,
                                           &pinterruptPvt);
    if(status!=asynSuccess) return status;
    pasynInt32ArrayInterrupt = pasynManager->memMalloc(
                                             sizeof(asynInt32ArrayInterrupt));
    pinterruptNode = pasynManager->createInterruptNode(pinterruptPvt);
    if(status!=asynSuccess) return status;
    pasynInt32ArrayInterrupt = (asynInt32ArrayInterrupt *)pinterruptNode->drvPvt;
    pasynInt32ArrayInterrupt->addr = addr;
    pasynInt32ArrayInterrupt->reason = pasynUser->reason;
    pasynInt32ArrayInterrupt->drvUser = pasynUser->drvUser;
    pasynInt32ArrayInterrupt->callback = callback;
    pasynInt32ArrayInterrupt->userPvt = userPvt;
    *registrarPvt = pinterruptNode;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d registerInterruptUser\n",portName,addr);
    return pasynManager->addInterruptUser(pinterruptNode);
}

static asynStatus cancelInterruptUser(void *registrarPvt, asynUser *pasynUser)
{
    interruptNode *pinterruptNode = (interruptNode *)registrarPvt;
    asynStatus status;
    const char *portName;
    int        addr;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d cancelInterruptUser\n",portName,addr);
    pasynManager->memFree(pinterruptNode->drvPvt, sizeof(asynInt32ArrayInterrupt));
    status = pasynManager->removeInterruptUser(pinterruptNode);
    return status;
}
