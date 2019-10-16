/*  asynInt64Base.c */
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

#include <epicsTypes.h>
#include <cantProceed.h>

#define epicsExportSharedSymbols
#include <shareLib.h>
#include "asynDriver.h"
#include "asynInt64.h"

static asynStatus initialize(const char *portName, asynInterface *pint64Interface);

static asynInt64Base int64Base = {initialize};
epicsShareDef asynInt64Base *pasynInt64Base = &int64Base;

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser,
                              epicsInt64 value);
static asynStatus readDefault(void *drvPvt, asynUser *pasynUser,
                              epicsInt64 *value);
static asynStatus getBounds(void *drvPvt, asynUser *pasynUser, 
                            epicsInt64 *low, epicsInt64 *high);
static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser,
                               interruptCallbackInt64 callback, void *userPvt,
                               void **registrarPvt);
static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser,
                               void *registrarPvt);


asynStatus initialize(const char *portName, asynInterface *pdriver)
{
    asynInt64 *pasynInt64 = (asynInt64 *)pdriver->pinterface;

    if(!pasynInt64->write) pasynInt64->write = writeDefault;
    if(!pasynInt64->read) pasynInt64->read = readDefault;
    if(!pasynInt64->getBounds) pasynInt64->getBounds = getBounds;
    if(!pasynInt64->registerInterruptUser)
        pasynInt64->registerInterruptUser = registerInterruptUser;
    if(!pasynInt64->cancelInterruptUser)
        pasynInt64->cancelInterruptUser = cancelInterruptUser;
    return pasynManager->registerInterface(portName,pdriver);
}

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser,
    epicsInt64 value)
{
    const char *portName;
    asynStatus status;
    int        addr;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
        "write is not supported");
    asynPrint(pasynUser,ASYN_TRACE_ERROR,
        "%s %d write is not supported\n",portName,addr);
    return asynError;
}

static asynStatus readDefault(void *drvPvt, asynUser *pasynUser,
    epicsInt64 *value)
{
    const char *portName;
    asynStatus status;
    int        addr;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
        "write is not supported");
    asynPrint(pasynUser,ASYN_TRACE_ERROR,
        "%s %d read is not supported\n",portName,addr);
    return asynError;
}

static asynStatus getBounds(void *drvPvt, asynUser *pasynUser, 
                            epicsInt64 *low, epicsInt64 *high)
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
                               interruptCallbackInt64 callback, void *userPvt,
                               void **registrarPvt)
{
    const char    *portName;
    asynStatus    status;
    int           addr;
    interruptNode *pinterruptNode;
    void          *pinterruptPvt;
    asynInt64Interrupt *pasynInt64Interrupt;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    status = pasynManager->getInterruptPvt(pasynUser, asynInt64Type, 
                                           &pinterruptPvt);
    if(status!=asynSuccess) return status;
    pasynInt64Interrupt = pasynManager->memMalloc(sizeof(asynInt64Interrupt));
    pinterruptNode = pasynManager->createInterruptNode(pinterruptPvt);
    pinterruptNode->drvPvt = pasynInt64Interrupt;
    pasynInt64Interrupt->pasynUser = 
                       pasynManager->duplicateAsynUser(pasynUser, NULL, NULL);
    pasynInt64Interrupt->addr = addr;
    pasynInt64Interrupt->callback = callback;
    pasynInt64Interrupt->userPvt = userPvt;
    *registrarPvt = pinterruptNode;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d registerInterruptUser\n",portName,addr);
    return pasynManager->addInterruptUser(pasynUser,pinterruptNode);
}

static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser,
    void *registrarPvt)
{
    interruptNode      *pinterruptNode = (interruptNode *)registrarPvt;
    asynInt64Interrupt *pasynInt64Interrupt = 
                             (asynInt64Interrupt *)pinterruptNode->drvPvt;
    asynStatus status;
    const char *portName;
    int        addr;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d cancelInterruptUser\n",portName,addr);
    status = pasynManager->removeInterruptUser(pasynUser,pinterruptNode);
    if(status==asynSuccess)
        pasynManager->freeInterruptNode(pasynUser, pinterruptNode);
    pasynManager->freeAsynUser(pasynInt64Interrupt->pasynUser);
    pasynManager->memFree(pasynInt64Interrupt, sizeof(asynInt64Interrupt));
    return status;
}
