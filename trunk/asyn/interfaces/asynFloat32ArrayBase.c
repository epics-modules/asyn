/*  asynFloat32ArrayBase.c */
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
#include "asynFloat32Array.h"

static asynStatus initialize(const char *portName, asynInterface *pfloat32ArrayInterface);

static asynFloat32ArrayBase float32ArrayBase = {initialize};
epicsShareDef asynFloat32ArrayBase *pasynFloat32ArrayBase = &float32ArrayBase;

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser,
                               epicsFloat32 *value, size_t nelem);
static asynStatus readDefault(void *drvPvt, asynUser *pasynUser,
                              epicsFloat32 *value, size_t nelem, size_t *nIn);
static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser,
                               interruptCallbackFloat32Array callback, void *userPvt,
                               void **registrarPvt);
static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser,
                               void *registrarPvt);


asynStatus initialize(const char *portName, asynInterface *pdriver)
{
    asynFloat32Array *pasynFloat32Array =
                      (asynFloat32Array *)pdriver->pinterface;

    if(!pasynFloat32Array->write) pasynFloat32Array->write = writeDefault;
    if(!pasynFloat32Array->read) pasynFloat32Array->read = readDefault;
    if(!pasynFloat32Array->registerInterruptUser)
        pasynFloat32Array->registerInterruptUser = registerInterruptUser;
    if(!pasynFloat32Array->cancelInterruptUser)
        pasynFloat32Array->cancelInterruptUser = cancelInterruptUser;
    return pasynManager->registerInterface(portName,pdriver);
}

static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser,
    epicsFloat32 *value, size_t nelem)
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
    epicsFloat32 *value, size_t nelem, size_t *nIn)
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
                               interruptCallbackFloat32Array callback, void *userPvt,
                               void **registrarPvt)
{
    const char    *portName;
    asynStatus    status;
    int           addr;
    interruptNode *pinterruptNode;
    void          *pinterruptPvt;
    asynFloat32ArrayInterrupt *pasynFloat32ArrayInterrupt;

    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    status = pasynManager->getInterruptPvt(pasynUser, asynFloat32ArrayType,
                                           &pinterruptPvt);
    if(status!=asynSuccess) return status;
    pinterruptNode = pasynManager->createInterruptNode(pinterruptPvt);
    if(status!=asynSuccess) return status;
    pasynFloat32ArrayInterrupt = pasynManager->memMalloc(
                                             sizeof(asynFloat32ArrayInterrupt));
    pinterruptNode->drvPvt = pasynFloat32ArrayInterrupt;
    pasynFloat32ArrayInterrupt->pasynUser =
                        pasynManager->duplicateAsynUser(pasynUser, NULL, NULL);
    pasynFloat32ArrayInterrupt->addr = addr;
    pasynFloat32ArrayInterrupt->callback = callback;
    pasynFloat32ArrayInterrupt->userPvt = userPvt;
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
    asynFloat32ArrayInterrupt *pasynFloat32ArrayInterrupt = 
        (asynFloat32ArrayInterrupt *)pinterruptNode->drvPvt;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d cancelInterruptUser\n",portName,addr);
    status = pasynManager->removeInterruptUser(pasynUser,pinterruptNode);
    pasynManager->freeAsynUser(pasynFloat32ArrayInterrupt->pasynUser);
    pasynManager->memFree(pasynFloat32ArrayInterrupt, sizeof(asynFloat32ArrayInterrupt));
    return status;
}
