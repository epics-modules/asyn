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

#include <epicsTypes.h>
#include <cantProceed.h>

#define epicsExportSharedSymbols
#include <shareLib.h>
#include "asynDriver.h"
#include "asynUInt32Digital.h"

static asynStatus initialize(const char *portName, asynInterface *pasynUInt32DigitalInterface);
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
static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser,
       void *registrarPvt);


static asynStatus initialize(const char *portName,asynInterface *pdriver)
{
    asynUInt32Digital *pasynUInt32Digital =
                      (asynUInt32Digital *)pdriver->pinterface;

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
    return pasynManager->registerInterface(portName,pdriver);
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
        "write is not supported");
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
        "write is not supported");
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
        "setInterrupt is not supported");
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
        "clearInterrupt is not supported");
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
        "getInterrupt is not supported");
    asynPrint(pasynUser,ASYN_TRACE_ERROR,
        "%s %d getInterrupt is not supported\n",portName,addr);
    return asynError;
}

static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser,
      interruptCallbackUInt32Digital callback, void *userPvt,epicsUInt32 mask,
      void **registrarPvt)
{
    const char    *portName;
    asynStatus    status;
    int           addr;
    interruptNode *pinterruptNode;
    void          *pinterruptPvt;
    asynUInt32DigitalInterrupt *pasynUInt32DigitalInterrupt;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    status = pasynManager->getInterruptPvt(pasynUser, asynUInt32DigitalType,
                                           &pinterruptPvt);
    if(status!=asynSuccess) return status;
    pinterruptNode = pasynManager->createInterruptNode(pinterruptPvt);
    if(status!=asynSuccess) return status;
    pasynUInt32DigitalInterrupt = pasynManager->memMalloc(
                                        sizeof(asynUInt32DigitalInterrupt));
    pinterruptNode->drvPvt = pasynUInt32DigitalInterrupt;
    pasynUInt32DigitalInterrupt->pasynUser =
                       pasynManager->duplicateAsynUser(pasynUser, NULL, NULL);
    pasynUInt32DigitalInterrupt->mask = mask;
    pasynUInt32DigitalInterrupt->addr = addr;
    pasynUInt32DigitalInterrupt->callback = callback;
    pasynUInt32DigitalInterrupt->userPvt = userPvt;
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
    asynUInt32DigitalInterrupt *pasynUInt32DigitalInterrupt =
           (asynUInt32DigitalInterrupt *)pinterruptNode->drvPvt;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d cancelInterruptUser\n",portName,addr);
    status = pasynManager->removeInterruptUser(pasynUser,pinterruptNode);
    pasynManager->freeAsynUser(pasynUInt32DigitalInterrupt->pasynUser);
    pasynManager->memFree(pasynUInt32DigitalInterrupt, 
                          sizeof(asynUInt32DigitalInterrupt));
    return status;
}
