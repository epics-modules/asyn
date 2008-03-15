/*  asynInt8ArrayBase.c */
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
#include "asynInt8Array.h"

static asynStatus initialize(const char *portName, asynInterface *pint8ArrayInterface);

static asynInt8ArrayBase int8ArrayBase = {initialize};
epicsShareDef asynInt8ArrayBase *pasynInt8ArrayBase = &int8ArrayBase;

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser,
                               epicsInt8 *value, size_t nelem);
static asynStatus readDefault(void *drvPvt, asynUser *pasynUser,
                              epicsInt8 *value, size_t nelem, size_t *nIn);
static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser,
                               interruptCallbackInt8Array callback, void *userPvt,
                               void **registrarPvt);
static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser,
                               void *registrarPvt);


asynStatus initialize(const char *portName, asynInterface *pdriver)
{
    asynInt8Array *pasynInt8Array = (asynInt8Array *)pdriver->pinterface;

    if(!pasynInt8Array->write) pasynInt8Array->write = writeDefault;
    if(!pasynInt8Array->read) pasynInt8Array->read = readDefault;
    if(!pasynInt8Array->registerInterruptUser)
        pasynInt8Array->registerInterruptUser = registerInterruptUser;
    if(!pasynInt8Array->cancelInterruptUser)
        pasynInt8Array->cancelInterruptUser = cancelInterruptUser;
    return pasynManager->registerInterface(portName,pdriver);
}

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser,
    epicsInt8 *value, size_t nelem)
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
    epicsInt8 *value, size_t nelem, size_t *nIn)
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
      interruptCallbackInt8Array callback, void *userPvt,void **registrarPvt)
{
    const char    *portName;
    asynStatus    status;
    int           addr;
    interruptNode *pinterruptNode;
    void          *pinterruptPvt;
    asynInt8ArrayInterrupt *pasynInt8ArrayInterrupt;

    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    status = pasynManager->getInterruptPvt(pasynUser, asynInt8ArrayType,
                                           &pinterruptPvt);
    if(status!=asynSuccess) return status;
    pinterruptNode = pasynManager->createInterruptNode(pinterruptPvt);
    if(status!=asynSuccess) return status;
    pasynInt8ArrayInterrupt = pasynManager->memMalloc(
                                             sizeof(asynInt8ArrayInterrupt));
    pinterruptNode->drvPvt = pasynInt8ArrayInterrupt;
    pasynInt8ArrayInterrupt->pasynUser =
                       pasynManager->duplicateAsynUser(pasynUser, NULL, NULL);
    pasynInt8ArrayInterrupt->addr = addr;
    pasynInt8ArrayInterrupt->callback = callback;
    pasynInt8ArrayInterrupt->userPvt = userPvt;
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
    asynInt8ArrayInterrupt *pasynInt8ArrayInterrupt =
                  (asynInt8ArrayInterrupt *)pinterruptNode->drvPvt;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d cancelInterruptUser\n",portName,addr);
    status = pasynManager->removeInterruptUser(pasynUser,pinterruptNode);
    pasynManager->freeAsynUser(pasynInt8ArrayInterrupt->pasynUser);
    pasynManager->memFree(pasynInt8ArrayInterrupt, sizeof(asynInt8ArrayInterrupt));
    return status;
}
