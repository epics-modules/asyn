/*  asynEnumBase.c */
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
#include "asynEnum.h"

static asynStatus initialize(const char *portName, asynInterface *pEnumInterface);

static asynEnumBase enumBase = {initialize};
epicsShareDef asynEnumBase *pasynEnumBase = &enumBase;

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser,
                              char *strings[], int values[], int severities[], size_t nElements);
static asynStatus readDefault(void *drvPvt, asynUser *pasynUser,
                              char *strings[], int values[], int severities[], size_t nElements, size_t *nIn);
static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser,
                               interruptCallbackEnum callback, void *userPvt,
                               void **registrarPvt);
static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser,
                               void *registrarPvt);


asynStatus initialize(const char *portName, asynInterface *pdriver)
{
    asynEnum *pasynEnum = (asynEnum *)pdriver->pinterface;

    if(!pasynEnum->write) pasynEnum->write = writeDefault;
    if(!pasynEnum->read) pasynEnum->read = readDefault;
    if(!pasynEnum->registerInterruptUser)
        pasynEnum->registerInterruptUser = registerInterruptUser;
    if(!pasynEnum->cancelInterruptUser)
        pasynEnum->cancelInterruptUser = cancelInterruptUser;
    return pasynManager->registerInterface(portName,pdriver);
}

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser,
    char *strings[], int values[], int severities[], size_t nElements)
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
    char *strings[], int values[], int severities[], size_t nElements, size_t *nIn)
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
                               interruptCallbackEnum callback, void *userPvt,
                               void **registrarPvt)
{
    const char    *portName;
    asynStatus    status;
    int           addr;
    interruptNode *pinterruptNode;
    void          *pinterruptPvt;
    asynEnumInterrupt *pasynEnumInterrupt;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    status = pasynManager->getInterruptPvt(pasynUser, asynEnumType, 
                                           &pinterruptPvt);
    if(status!=asynSuccess) return status;
    pasynEnumInterrupt = pasynManager->memMalloc(sizeof(asynEnumInterrupt));
    pinterruptNode = pasynManager->createInterruptNode(pinterruptPvt);
    pinterruptNode->drvPvt = pasynEnumInterrupt;
    pasynEnumInterrupt->pasynUser = 
                       pasynManager->duplicateAsynUser(pasynUser, NULL, NULL);
    pasynEnumInterrupt->addr = addr;
    pasynEnumInterrupt->callback = callback;
    pasynEnumInterrupt->userPvt = userPvt;
    *registrarPvt = pinterruptNode;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d registerInterruptUser\n",portName,addr);
    return pasynManager->addInterruptUser(pasynUser,pinterruptNode);
}

static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser,
    void *registrarPvt)
{
    interruptNode      *pinterruptNode = (interruptNode *)registrarPvt;
    asynEnumInterrupt *pasynEnumInterrupt = 
                             (asynEnumInterrupt *)pinterruptNode->drvPvt;
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
        pasynManager->freeInterruptNode(pasynUser,pinterruptNode);
    pasynManager->freeAsynUser(pasynEnumInterrupt->pasynUser);
    pasynManager->memFree(pasynEnumInterrupt, sizeof(asynEnumInterrupt));
    return status;
}
