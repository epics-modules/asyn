/*  asynHandleBase.c
 *
 ***********************************************************************
 * Copyright (c) 2002 The University of Chicago, as Operator of Argonne
 * National Laboratory, and the Regents of the University of
 * California, as Operator of Los Alamos National Laboratory, and
 * Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
 * asynDriver is distributed subject to a Software License Agreement
 * found in file LICENSE that is included with this distribution.
 ***********************************************************************
 *
 *  31-March-2008 Mark Rivers
 */

#include <epicsTypes.h>
#include <epicsStdio.h>
#include <cantProceed.h>

#define epicsExportSharedSymbols
#include <shareLib.h>
#include <asynDriver.h>

#include "asynHandle.h"

static asynStatus initialize(const char *portName, asynInterface *pHandleInterface);

static asynHandleBase HandleBase = {initialize};
epicsShareDef asynHandleBase *pasynHandleBase = &HandleBase;

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser, void *handle);
static asynStatus readDefault(void *drvPvt, asynUser *pasynUser, void *handle);
static asynStatus registerInterruptUser(void *drvPvt, asynUser *pasynUser,
                    interruptCallbackHandle callback, void *userPvt, void **registrarPvt);
static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser,
                    void *registrarPvt);


static asynStatus initialize(const char *portName, asynInterface *pdriver)
{
    asynHandle   *pasynHandle = (asynHandle *)pdriver->pinterface;

    if(!pasynHandle->write) pasynHandle->write = writeDefault;
    if(!pasynHandle->read) pasynHandle->read = readDefault;
    if(!pasynHandle->registerInterruptUser)
        pasynHandle->registerInterruptUser = registerInterruptUser;
    if(!pasynHandle->cancelInterruptUser)
        pasynHandle->cancelInterruptUser = cancelInterruptUser;
    return pasynManager->registerInterface(portName, pdriver);
}

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser, void *handle)
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

static asynStatus readDefault(void *drvPvt, asynUser *pasynUser, void *handle)
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
      interruptCallbackHandle callback, void *userPvt, void **registrarPvt)
{
    const char    *portName;
    asynStatus    status;
    int           addr;
    interruptNode *pinterruptNode;
    void          *pinterruptPvt;
    asynHandleInterrupt *pasynHandleInterrupt;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    status = pasynManager->getInterruptPvt(pasynUser, asynHandleType,
                                           &pinterruptPvt);
    if(status!=asynSuccess) return status;
    pinterruptNode = pasynManager->createInterruptNode(pinterruptPvt);
    if(status!=asynSuccess) return status;
    pasynHandleInterrupt = pasynManager->memMalloc(sizeof(asynHandleInterrupt));
    pinterruptNode->drvPvt = pasynHandleInterrupt;
    pasynHandleInterrupt->pasynUser =
                        pasynManager->duplicateAsynUser(pasynUser, NULL, NULL);
    pasynHandleInterrupt->addr = addr;
    pasynHandleInterrupt->callback = callback;
    pasynHandleInterrupt->userPvt = userPvt;
    *registrarPvt = pinterruptNode;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d registerInterruptUser\n",portName,addr);
    return pasynManager->addInterruptUser(pasynUser,pinterruptNode);
}

static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser,
    void *registrarPvt)
{
    interruptNode *pinterruptNode = (interruptNode *)registrarPvt;
    asynStatus    status;
    const char    *portName;
    int           addr;
    asynHandleInterrupt *pasynHandleInterrupt = 
                                (asynHandleInterrupt *)pinterruptNode->drvPvt;

    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d cancelInterruptUser\n",portName,addr);
    status = pasynManager->removeInterruptUser(pasynUser,pinterruptNode);
    pasynManager->freeAsynUser(pasynHandleInterrupt->pasynUser);
    pasynManager->memFree(pasynHandleInterrupt, sizeof(asynHandleInterrupt));
    return status;
}
