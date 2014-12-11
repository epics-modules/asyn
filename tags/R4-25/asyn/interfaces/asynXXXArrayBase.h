#define ASYN_XXX_ARRAY_BASE_FUNCS(INTERFACE, INTERFACE_TYPE, INTERFACE_BASE, PINTERFACE_BASE,\
                                  INTERRUPT, INTERRUPT_CALLBACK, EPICS_TYPE)\
/*  asynXXXArrayBase.h */ \
/*********************************************************************** \
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne \
* National Laboratory, and the Regents of the University of \
* California, as Operator of Los Alamos National Laboratory, and \
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY). \
* asynDriver is distributed subject to a Software License Agreement \
* found in file LICENSE that is included with this distribution. \
***********************************************************************/ \
 \
/*  11-OCT-2004 Marty Kraimer \
 *  26-MAR-2008 Mark Rivers, converted to giant macro \
*/ \
 \
static asynStatus initialize(const char *portName, asynInterface *pInterface); \
 \
static INTERFACE_BASE arrayBase = {initialize}; \
epicsShareDef INTERFACE_BASE *PINTERFACE_BASE = &arrayBase; \
 \
static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser, \
                               EPICS_TYPE *value, size_t nelem); \
static asynStatus readDefault(void *drvPvt, asynUser *pasynUser, \
                               EPICS_TYPE *value, size_t nelem, size_t *nIn); \
static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser, \
                               INTERRUPT_CALLBACK callback, void *userPvt, \
                               void **registrarPvt); \
static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser, \
                               void *registrarPvt); \
 \
 \
asynStatus initialize(const char *portName, asynInterface *pdriver) \
{ \
    INTERFACE *pInterface = (INTERFACE *)pdriver->pinterface; \
 \
    if(!pInterface->write) pInterface->write = writeDefault; \
    if(!pInterface->read) pInterface->read = readDefault; \
    if(!pInterface->registerInterruptUser) \
        pInterface->registerInterruptUser = registerInterruptUser; \
    if(!pInterface->cancelInterruptUser) \
        pInterface->cancelInterruptUser = cancelInterruptUser; \
    return pasynManager->registerInterface(portName,pdriver); \
} \
 \
static asynStatus writeDefault(void *drvPvt, asynUser *pasynUser, \
    EPICS_TYPE *value, size_t nelem) \
{ \
    const char *portName; \
    asynStatus status; \
    int        addr; \
     \
    status = pasynManager->getPortName(pasynUser,&portName); \
    if(status!=asynSuccess) return status; \
    status = pasynManager->getAddr(pasynUser,&addr); \
    if(status!=asynSuccess) return status; \
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize, \
        "write is not supported"); \
    asynPrint(pasynUser,ASYN_TRACE_ERROR, \
        "%s %d write is not supported\n",portName,addr); \
    return asynError; \
} \
 \
static asynStatus readDefault(void *drvPvt, asynUser *pasynUser, \
    EPICS_TYPE *value, size_t nelem, size_t *nIn) \
{ \
    const char *portName; \
    asynStatus status; \
    int        addr; \
     \
    status = pasynManager->getPortName(pasynUser,&portName); \
    if(status!=asynSuccess) return status; \
    status = pasynManager->getAddr(pasynUser,&addr); \
    if(status!=asynSuccess) return status; \
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize, \
        "write is not supported"); \
    asynPrint(pasynUser,ASYN_TRACE_ERROR, \
        "%s %d read is not supported\n",portName,addr); \
    return asynError; \
} \
 \
 \
static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser, \
      INTERRUPT_CALLBACK callback, void *userPvt,void **registrarPvt) \
{ \
    const char    *portName; \
    asynStatus    status; \
    int           addr; \
    interruptNode *pinterruptNode; \
    void          *pinterruptPvt; \
    INTERRUPT     *pInterrupt; \
 \
    status = pasynManager->getPortName(pasynUser,&portName); \
    if(status!=asynSuccess) return status; \
    status = pasynManager->getAddr(pasynUser,&addr); \
    if(status!=asynSuccess) return status; \
    status = pasynManager->getInterruptPvt(pasynUser, INTERFACE_TYPE, \
                                           &pinterruptPvt); \
    if(status!=asynSuccess) return status; \
    pinterruptNode = pasynManager->createInterruptNode(pinterruptPvt); \
    if(status!=asynSuccess) return status; \
    pInterrupt = pasynManager->memMalloc( \
                                             sizeof(INTERRUPT)); \
    pinterruptNode->drvPvt = pInterrupt; \
    pInterrupt->pasynUser = \
                       pasynManager->duplicateAsynUser(pasynUser, NULL, NULL); \
    pInterrupt->addr = addr; \
    pInterrupt->callback = callback; \
    pInterrupt->userPvt = userPvt; \
    *registrarPvt = pinterruptNode; \
    asynPrint(pasynUser,ASYN_TRACE_FLOW, \
        "%s %d registerInterruptUser\n",portName,addr); \
    return pasynManager->addInterruptUser(pasynUser,pinterruptNode); \
} \
 \
static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser,void *registrarPvt) \
{ \
    interruptNode *pinterruptNode = (interruptNode *)registrarPvt; \
    asynStatus    status; \
    const char    *portName; \
    int           addr; \
    INTERRUPT *pInterrupt = \
              (INTERRUPT *)pinterruptNode->drvPvt; \
     \
    status = pasynManager->getPortName(pasynUser,&portName); \
    if(status!=asynSuccess) return status; \
    status = pasynManager->getAddr(pasynUser,&addr); \
    if(status!=asynSuccess) return status; \
    asynPrint(pasynUser,ASYN_TRACE_FLOW, \
        "%s %d cancelInterruptUser\n",portName,addr); \
    status = pasynManager->removeInterruptUser(pasynUser,pinterruptNode); \
    pasynManager->freeAsynUser(pInterrupt->pasynUser); \
    pasynManager->memFree(pInterrupt, sizeof(INTERRUPT)); \
    return status; \
}
