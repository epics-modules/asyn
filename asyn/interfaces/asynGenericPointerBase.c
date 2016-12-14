/*  asynGenericPointerBase.c
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

#include "asynGenericPointer.h"

static asynStatus initialize(const char *portName, asynInterface *pGenericPointerInterface);

static asynGenericPointerBase GenericPointerBase = {initialize};
epicsShareDef asynGenericPointerBase *pasynGenericPointerBase = &GenericPointerBase;

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser, void *pointer);
static asynStatus readDefault(void *drvPvt, asynUser *pasynUser, void *pointer);
static asynStatus registerInterruptUser(void *drvPvt, asynUser *pasynUser,
                    interruptCallbackGenericPointer callback, void *userPvt, void **registrarPvt);
static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser,
                    void *registrarPvt);


static asynStatus initialize(const char *portName, asynInterface *pdriver)
{
    asynGenericPointer   *pasynGenericPointer = (asynGenericPointer *)pdriver->pinterface;

    if(!pasynGenericPointer->write) pasynGenericPointer->write = writeDefault;
    if(!pasynGenericPointer->read) pasynGenericPointer->read = readDefault;
    if(!pasynGenericPointer->registerInterruptUser)
        pasynGenericPointer->registerInterruptUser = registerInterruptUser;
    if(!pasynGenericPointer->cancelInterruptUser)
        pasynGenericPointer->cancelInterruptUser = cancelInterruptUser;
    return pasynManager->registerInterface(portName, pdriver);
}

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser, void *pointer)
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

static asynStatus readDefault(void *drvPvt, asynUser *pasynUser, void *pointer)
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
    
static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser,
      interruptCallbackGenericPointer callback, void *userPvt, void **registrarPvt)
{
    const char    *portName;
    asynStatus    status;
    int           addr;
    interruptNode *pinterruptNode;
    void          *pinterruptPvt;
    asynGenericPointerInterrupt *pasynGenericPointerInterrupt;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    status = pasynManager->getInterruptPvt(pasynUser, asynGenericPointerType,
                                           &pinterruptPvt);
    if(status!=asynSuccess) return status;
    pinterruptNode = pasynManager->createInterruptNode(pinterruptPvt);
    if(status!=asynSuccess) return status;
    pasynGenericPointerInterrupt = pasynManager->memMalloc(sizeof(asynGenericPointerInterrupt));
    pinterruptNode->drvPvt = pasynGenericPointerInterrupt;
    pasynGenericPointerInterrupt->pasynUser =
                        pasynManager->duplicateAsynUser(pasynUser, NULL, NULL);
    pasynGenericPointerInterrupt->addr = addr;
    pasynGenericPointerInterrupt->callback = callback;
    pasynGenericPointerInterrupt->userPvt = userPvt;
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
    asynGenericPointerInterrupt *pasynGenericPointerInterrupt = 
                                (asynGenericPointerInterrupt *)pinterruptNode->drvPvt;

    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d cancelInterruptUser\n",portName,addr);
    status = pasynManager->removeInterruptUser(pasynUser,pinterruptNode);
    if(status==asynSuccess)
        pasynManager->freeInterruptNode(pasynUser,pinterruptNode);
    pasynManager->freeAsynUser(pasynGenericPointerInterrupt->pasynUser);
    pasynManager->memFree(pasynGenericPointerInterrupt, sizeof(asynGenericPointerInterrupt));
    return status;
}
