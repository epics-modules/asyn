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

#include <epicsTypes.h>
#include <cantProceed.h>

#define epicsExportSharedSymbols
#include <shareLib.h>
#include "asynDriver.h"
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
static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser,
                               void *registrarPvt);


asynStatus initialize(const char *portName, asynInterface *pdriver)
{
    asynInt32Array *pasynInt32Array = (asynInt32Array *)pdriver->pinterface;

    if(!pasynInt32Array->write) pasynInt32Array->write = writeDefault;
    if(!pasynInt32Array->read) pasynInt32Array->read = readDefault;
    if(!pasynInt32Array->registerInterruptUser)
        pasynInt32Array->registerInterruptUser = registerInterruptUser;
    if(!pasynInt32Array->cancelInterruptUser)
        pasynInt32Array->cancelInterruptUser = cancelInterruptUser;
    return pasynManager->registerInterface(portName,pdriver);
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
      interruptCallbackInt32Array callback, void *userPvt,void **registrarPvt)
{
    const char    *portName;
    asynStatus    status;
    int           addr;
    interruptNode *pinterruptNode;
    void          *pinterruptPvt;
    asynInt32ArrayInterrupt *pasynInt32ArrayInterrupt;

    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    status = pasynManager->getInterruptPvt(pasynUser, asynInt32ArrayType,
                                           &pinterruptPvt);
    if(status!=asynSuccess) return status;
    pinterruptNode = pasynManager->createInterruptNode(pinterruptPvt);
    if(status!=asynSuccess) return status;
    pasynInt32ArrayInterrupt = pasynManager->memMalloc(
                                             sizeof(asynInt32ArrayInterrupt));
    pinterruptNode->drvPvt = pasynInt32ArrayInterrupt;
    pasynInt32ArrayInterrupt->pasynUser =
                       pasynManager->duplicateAsynUser(pasynUser, NULL, NULL);
    pasynInt32ArrayInterrupt->addr = addr;
    pasynInt32ArrayInterrupt->callback = callback;
    pasynInt32ArrayInterrupt->userPvt = userPvt;
    *registrarPvt = pinterruptNode;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d registerInterruptUser\n",portName,addr);
    return pasynManager->addInterruptUser(pasynUser,pinterruptNode);
}

static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser,void *registrarPvt)
{
    interruptNode *pinterruptNode = (interruptNode *)registrarPvt;
    asynStatus    status;
    const char    *portName;
    int           addr;
    asynInt32ArrayInterrupt *pasynInt32ArrayInterrupt =
                  (asynInt32ArrayInterrupt *)pinterruptNode->drvPvt;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d cancelInterruptUser\n",portName,addr);
    status = pasynManager->removeInterruptUser(pasynUser,pinterruptNode);
    pasynManager->freeAsynUser(pasynInt32ArrayInterrupt->pasynUser);
    pasynManager->memFree(pasynInt32ArrayInterrupt, sizeof(asynInt32ArrayInterrupt));
    return status;
}
