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

#include <epicsTypes.h>
#include <cantProceed.h>

#define epicsExportSharedSymbols
#include <shareLib.h>
#include "asynDriver.h"
#include "asynInt32.h"

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
static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser,
                               void *registrarPvt);


asynStatus initialize(const char *portName, asynInterface *pdriver)
{
    asynInt32 *pasynInt32 = (asynInt32 *)pdriver->pinterface;

    if(!pasynInt32->write) pasynInt32->write = writeDefault;
    if(!pasynInt32->read) pasynInt32->read = readDefault;
    if(!pasynInt32->getBounds) pasynInt32->getBounds = getBounds;
    if(!pasynInt32->registerInterruptUser)
        pasynInt32->registerInterruptUser = registerInterruptUser;
    if(!pasynInt32->cancelInterruptUser)
        pasynInt32->cancelInterruptUser = cancelInterruptUser;
    return pasynManager->registerInterface(portName,pdriver);
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
        "write is not supported");
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
        "write is not supported");
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
    const char    *portName;
    asynStatus    status;
    int           addr;
    interruptNode *pinterruptNode;
    void          *pinterruptPvt;
    asynInt32Interrupt *pasynInt32Interrupt;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    status = pasynManager->getInterruptPvt(pasynUser, asynInt32Type, 
                                           &pinterruptPvt);
    if(status!=asynSuccess) return status;
    pasynInt32Interrupt = pasynManager->memMalloc(sizeof(asynInt32Interrupt));
    pinterruptNode = pasynManager->createInterruptNode(pinterruptPvt);
    pinterruptNode->drvPvt = pasynInt32Interrupt;
    pasynInt32Interrupt->pasynUser = 
                       pasynManager->duplicateAsynUser(pasynUser, NULL, NULL);
    pasynInt32Interrupt->addr = addr;
    pasynInt32Interrupt->callback = callback;
    pasynInt32Interrupt->userPvt = userPvt;
    *registrarPvt = pinterruptNode;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d registerInterruptUser\n",portName,addr);
    return pasynManager->addInterruptUser(pasynUser,pinterruptNode);
}

static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser,
    void *registrarPvt)
{
    interruptNode      *pinterruptNode = (interruptNode *)registrarPvt;
    asynInt32Interrupt *pasynInt32Interrupt = 
                             (asynInt32Interrupt *)pinterruptNode->drvPvt;
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
    pasynManager->freeAsynUser(pasynInt32Interrupt->pasynUser);
    pasynManager->memFree(pasynInt32Interrupt, sizeof(asynInt32Interrupt));
    return status;
}
