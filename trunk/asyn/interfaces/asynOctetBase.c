/*  asynOctetBase.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*  26-OCT-2004 Marty Kraimer */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <cantProceed.h>
#include <epicsAssert.h>
#include <epicsStdio.h>
#include <epicsString.h>

#include <asynDriver.h>
#include <epicsTypes.h>

#define epicsExportSharedSymbols

#include "asynOctet.h"
#include "asynInterposeEos.h"

#define overrideWrite        0x001
#define overrideRead         0x002
#define overrideFlush        0x004

typedef struct octetPvt {
    asynInterface octetBase; /*Implemented by asynOctetBase*/
    asynOctet     *pasynOctet; /*driver*/
    void          *drvPvt;
    int           override;
    void          *pasynPvt;   /*For registerInterruptSource*/
    int           interruptProcess;
}octetPvt;

typedef struct interruptPvt {
    interruptCallbackOctet callback;
    void                  *userPvt;
}interruptPvt;

static void initOverride(octetPvt *poctetPvt, asynOctet *pasynOctet);

static asynStatus initialize(const char *portName,
           asynInterface *poctetInterface,
           int processEosIn,int processEosOut,
           int interruptProcess);
static void callInterruptUsers(asynUser *pasynUser,void *pasynPvt,
    char *data,size_t *nbytesTransfered,int *eomReason);

static asynOctetBase octetBase = {initialize,callInterruptUsers};
epicsShareDef asynOctetBase *pasynOctetBase = &octetBase;

static asynStatus writeIt(void *drvPvt, asynUser *pasynUser,
    const char *data,size_t numchars,size_t *nbytesTransfered);
static asynStatus writeRaw(void *drvPvt, asynUser *pasynUser,
    const char *data,size_t numchars,size_t *nbytesTransfered);
static asynStatus readIt(void *drvPvt, asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason);
static asynStatus readRaw(void *drvPvt, asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason);
static asynStatus flushIt(void *drvPvt,asynUser *pasynUser);
static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser,
       interruptCallbackOctet callback, void *userPvt, void **registrarPvt);
static asynStatus cancelInterruptUser(void *registrarPvt, asynUser *pasynUser);
static asynStatus setInputEos(void *drvPvt,asynUser *pasynUser,
                        const char *eos,int eoslen);
static asynStatus getInputEos(void *drvPvt,asynUser *pasynUser,
                       char *eos, int eossize, int *eoslen);
static asynStatus setOutputEos(void *drvPvt,asynUser *pasynUser,
                        const char *eos,int eoslen);
static asynStatus getOutputEos(void *drvPvt,asynUser *pasynUser,
                       char *eos, int eossize, int *eoslen);

static asynOctet octet = {
    writeIt,writeRaw,readIt,readRaw,flushIt,
    registerInterruptUser,cancelInterruptUser,
    setInputEos,getInputEos,setOutputEos,getOutputEos
};
/*Implementation to replace null methods*/
static asynStatus writeFail(void *drvPvt, asynUser *pasynUser,
    const char *data,size_t numchars,size_t *nbytesTransfered);
static asynStatus writeRawFail(void *drvPvt, asynUser *pasynUser,
    const char *data,size_t numchars,size_t *nbytesTransfered);
static asynStatus readFail(void *drvPvt, asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason);
static asynStatus readRawFail(void *drvPvt, asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason);
static asynStatus flushFail(void *drvPvt,asynUser *pasynUser);
static asynStatus registerInterruptUserFail(void *drvPvt,asynUser *pasynUser,
       interruptCallbackOctet callback, void *userPvt, void **registrarPvt);
static asynStatus cancelInterruptUserFail(void *registrarPvt, asynUser *pasynUser);
static asynStatus setInputEosFail(void *drvPvt,asynUser *pasynUser,
                        const char *eos,int eoslen);
static asynStatus getInputEosFail(void *drvPvt,asynUser *pasynUser,
                       char *eos, int eossize, int *eoslen);
static asynStatus setOutputEosFail(void *drvPvt,asynUser *pasynUser,
                        const char *eos,int eoslen);
static asynStatus getOutputEosFail(void *drvPvt,asynUser *pasynUser,
                       char *eos, int eossize, int *eoslen);

static void initOverride(octetPvt *poctetPvt, asynOctet *pasynOctet)
{
    int        override = 0;

    if(!pasynOctet->write) pasynOctet->write = writeFail;
    if(pasynOctet->write == writeFail) override |= overrideWrite;
    if(!pasynOctet->writeRaw) pasynOctet->writeRaw = writeRawFail;
    if(!pasynOctet->read)  pasynOctet->read = readFail;
    if(pasynOctet->read == readFail) override |=  overrideRead;
    if(!pasynOctet->readRaw) pasynOctet->readRaw = readRawFail;
    if(!pasynOctet->flush) pasynOctet->flush = flushFail;
    if(pasynOctet->flush == flushFail) override |=  overrideFlush;
    pasynOctet->registerInterruptUser = registerInterruptUserFail;
    pasynOctet->cancelInterruptUser = cancelInterruptUserFail;
    if(!pasynOctet->setInputEos) pasynOctet->setInputEos = setInputEosFail;
    if(!pasynOctet->getInputEos) pasynOctet->getInputEos = getInputEosFail;
    if(!pasynOctet->setOutputEos) pasynOctet->setOutputEos = setOutputEosFail;
    if(!pasynOctet->getOutputEos) pasynOctet->getOutputEos = getOutputEosFail;
    poctetPvt->override = override;
}

static asynStatus initialize(const char *portName,
    asynInterface *pinterface,
    int processEosIn,int processEosOut,
    int interruptProcess)
{
    octetPvt   *poctetPvt;
    asynOctet  *pasynOctetDriver;
    asynStatus status;
    asynUser   *pasynUser;
    int        yesNo;

    pasynOctetDriver = (asynOctet *)pinterface->pinterface;
    poctetPvt = callocMustSucceed(1,sizeof(octetPvt),
        "asynOctetBase:initialize");
    poctetPvt->octetBase.interfaceType = asynOctetType;
    poctetPvt->octetBase.pinterface = &octet;
    poctetPvt->octetBase.drvPvt = poctetPvt;
    poctetPvt->pasynOctet = pasynOctetDriver;
    poctetPvt->drvPvt = pinterface->drvPvt;
    initOverride(poctetPvt,pasynOctetDriver);
    pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->isMultiDevice(pasynUser,portName,&yesNo);
    pasynManager->freeAsynUser(pasynUser);
    if(status!=asynSuccess) {
        printf("isMultiDevice failed %s\n",pasynUser->errorMessage);
        free(poctetPvt);
        return status;
    }
    if(yesNo && (processEosIn || processEosOut || interruptProcess)) {
        printf("Can not processEosIn, processEosOut,interruptProcess "
               "for multiDevice port\n");
        free(poctetPvt);
        return asynError;
    }
    status = pasynManager->registerInterface(portName,pinterface);
    if(status!=asynSuccess) return status;
    status = pasynManager->interposeInterface(
        portName,-1,&poctetPvt->octetBase,0);
    if(status!=asynSuccess) return status;
    poctetPvt->interruptProcess = interruptProcess;
    if(interruptProcess) {
        status = pasynManager->registerInterruptSource(
            portName,&poctetPvt->octetBase,&poctetPvt->pasynPvt);
        if(status!=asynSuccess) {
            printf("registerInterruptSource failed\n");
            return status;
        }
    }
    if(processEosIn || processEosOut) {
        asynInterposeEosConfig(portName,-1,processEosIn,processEosOut);
    }
    return asynSuccess;
}

static void callInterruptUsers(asynUser *pasynUser,void *pasynPvt,
    char *data,size_t *nbytesTransfered,int *eomReason)
{
    asynStatus         status;
    ELLLIST            *plist;
    interruptNode      *pnode;
    asynOctetInterrupt *pinterrupt;
    const char         *portName;
    int                addr;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status==asynSuccess)
        status = pasynManager->getPortName(pasynUser,&portName);
    if(status==asynSuccess) 
        status = pasynManager->interruptStart(pasynPvt,&plist);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s asynOctetBase callInterruptUsers failed %s\n",
            portName,pasynUser->errorMessage);
        return;
    }
    pnode = (interruptNode *)ellFirst(plist);
    if(pnode) {
        asynPrint(pasynUser,ASYN_TRACEIO_FILTER,
            "%s asynOctetBase interrupt\n",portName);
    }
    while (pnode) {
        pinterrupt = pnode->drvPvt;
        if(addr==pinterrupt->addr) {
            pinterrupt->callback(
                pinterrupt->userPvt,pinterrupt->pasynUser,
                data,*nbytesTransfered,*eomReason);
        }
        pnode = (interruptNode *)ellNext(&pnode->node);
    }
    pasynManager->interruptEnd(pasynPvt);
}

static asynStatus writeIt(void *drvPvt, asynUser *pasynUser,
    const char *data,size_t numchars,size_t *nbytesTransfered)
{
    octetPvt  *poctetPvt = (octetPvt *)drvPvt;
    asynOctet *pasynOctet = poctetPvt->pasynOctet;

    if(!(poctetPvt->override&overrideWrite)) {
        return pasynOctet->write(poctetPvt->drvPvt,
                                 pasynUser,data,numchars,nbytesTransfered);
    }
    return writeRaw(drvPvt,pasynUser,data,numchars,nbytesTransfered);
}

static asynStatus writeRaw(void *drvPvt, asynUser *pasynUser,
    const char *data,size_t numchars,size_t *nbytesTransfered)
{
    octetPvt *poctetPvt = (octetPvt *)drvPvt;
    asynOctet *pasynOctet = poctetPvt->pasynOctet;

    return pasynOctet->writeRaw(poctetPvt->drvPvt,pasynUser,
                      data,numchars,nbytesTransfered);
}

static asynStatus readIt(void *drvPvt, asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason)
{
    octetPvt   *poctetPvt = (octetPvt *)drvPvt;
    asynOctet  *pasynOctet = poctetPvt->pasynOctet;
    asynStatus status;

    if(!(poctetPvt->override&overrideRead)) {
        status = pasynOctet->read(poctetPvt->drvPvt,pasynUser,
                                 data,maxchars,nbytesTransfered,eomReason);
    } else { 
        status = readRaw(drvPvt,pasynUser,
           data,maxchars,nbytesTransfered,eomReason);
    }
    if(status!=asynSuccess) return status;
    if(poctetPvt->interruptProcess)
        callInterruptUsers(pasynUser,poctetPvt->pasynPvt,
            data,nbytesTransfered,eomReason);
    return status;
}

static asynStatus readRaw(void *drvPvt, asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason)
{
    octetPvt *poctetPvt = (octetPvt *)drvPvt;
    asynOctet *pasynOctet = poctetPvt->pasynOctet;
    asynStatus status;

    status = pasynOctet->readRaw(poctetPvt->drvPvt,pasynUser,
                 data,maxchars,nbytesTransfered,eomReason);
    if(status!=asynSuccess) return status;
    if(poctetPvt->interruptProcess)
        callInterruptUsers(pasynUser,poctetPvt->pasynPvt,
            data,nbytesTransfered,eomReason);
    return asynSuccess;
}

static asynStatus flushIt(void *drvPvt,asynUser *pasynUser)
{
    octetPvt   *poctetPvt = (octetPvt *)drvPvt;
    asynOctet *pasynOctet = poctetPvt->pasynOctet;
    double    savetimeout = pasynUser->timeout;
    char         buffer[100]; 
    asynStatus   status;
    size_t       nbytesTransfered;


    if(!(poctetPvt->override&overrideFlush)) {
        return pasynOctet->flush(poctetPvt->drvPvt,pasynUser);
    }
    pasynUser->timeout = .05;
    while(1) {
        nbytesTransfered = 0;
        status = pasynOctet->read(poctetPvt->drvPvt,pasynUser,
            buffer,sizeof(buffer),&nbytesTransfered,0);
        if(nbytesTransfered==0) break;
        asynPrintIO(pasynUser,ASYN_TRACEIO_FILTER,
            buffer,nbytesTransfered,"asynOctetBase:flush ");
    }
    pasynUser->timeout = savetimeout;
    return asynSuccess;
}

static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser,
      interruptCallbackOctet callback, void *userPvt, void **registrarPvt)
{
    const char         *portName;
    asynStatus         status;
    int                addr;
    interruptNode      *pinterruptNode;
    asynOctetInterrupt *pasynOctetInterrupt;
    void               *pinterruptPvt;
    
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getInterruptPvt(pasynUser, asynOctetType,
                                            &pinterruptPvt);
    if(status!=asynSuccess) return status;
    pinterruptNode = pasynManager->createInterruptNode(pinterruptPvt);
    if(status!=asynSuccess) return status;
    pasynOctetInterrupt = pasynManager->memMalloc(sizeof(asynOctetInterrupt));
    pinterruptNode->drvPvt = pasynOctetInterrupt;
    pasynOctetInterrupt->pasynUser =
        pasynManager->duplicateAsynUser(pasynUser, NULL, NULL);
    pasynOctetInterrupt->addr = addr;
    pasynOctetInterrupt->callback = callback;
    pasynOctetInterrupt->userPvt = userPvt;
    *registrarPvt = pinterruptNode;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d registerInterruptUser\n",portName,addr);
    return pasynManager->addInterruptUser(pasynUser,pinterruptNode);
}

static asynStatus cancelInterruptUser(void *registrarPvt, asynUser *pasynUser)
{
    interruptNode *pinterruptNode = (interruptNode *)registrarPvt;
    asynOctetInterrupt *pinterrupt = (asynOctetInterrupt *)pinterruptNode->drvPvt;
    asynStatus    status;
    const char    *portName;
    int           addr;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s %d cancelInterruptUser\n",portName,addr);
    pasynManager->freeAsynUser(pinterrupt->pasynUser);
    pasynManager->memFree(pinterruptNode->drvPvt,sizeof(asynOctetInterrupt));
    status = pasynManager->removeInterruptUser(pasynUser,pinterruptNode);
    if(status==asynSuccess)
        pasynManager->freeInterruptNode(pasynUser,pinterruptNode);
    return status;
}

static asynStatus setInputEos(void *drvPvt,asynUser *pasynUser,
                        const char *eos,int eoslen)
{
    octetPvt  *poctetPvt = (octetPvt *)drvPvt;
    asynOctet *pasynOctet = poctetPvt->pasynOctet;

    return pasynOctet->setInputEos(poctetPvt->drvPvt,pasynUser, eos,eoslen);
}

static asynStatus getInputEos(void *drvPvt,asynUser *pasynUser,
                       char *eos, int eossize, int *eoslen)
{
    octetPvt  *poctetPvt = (octetPvt *)drvPvt;
    asynOctet *pasynOctet = poctetPvt->pasynOctet;

    return pasynOctet->getInputEos(poctetPvt->drvPvt,pasynUser,
                     eos,eossize,eoslen);
}

static asynStatus setOutputEos(void *drvPvt,asynUser *pasynUser,
                        const char *eos,int eoslen)
{
    octetPvt  *poctetPvt = (octetPvt *)drvPvt;
    asynOctet *pasynOctet = poctetPvt->pasynOctet;

    return pasynOctet->setOutputEos(poctetPvt->drvPvt,pasynUser, eos,eoslen);
}

static asynStatus getOutputEos(void *drvPvt,asynUser *pasynUser,
                       char *eos, int eossize, int *eoslen)
{
    octetPvt  *poctetPvt = (octetPvt *)drvPvt;
    asynOctet *pasynOctet = poctetPvt->pasynOctet;

    return pasynOctet->getOutputEos(poctetPvt->drvPvt,pasynUser,
                 eos,eossize,eoslen);
}

static asynStatus showFailure(asynUser *pasynUser,const char *method)
{
    const char *portName;
    asynStatus status;
    
    status = pasynManager->getPortName(pasynUser,&portName);
    if(status!=asynSuccess) return status;
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
        "%s %s not implemented\n",portName,method);
    return asynError;
}

static asynStatus writeFail(void *drvPvt, asynUser *pasynUser,
    const char *data,size_t numchars,size_t *nbytesTransfered)
{ return showFailure(pasynUser,"write");}

static asynStatus writeRawFail(void *drvPvt, asynUser *pasynUser,
    const char *data,size_t numchars,size_t *nbytesTransfered)
{ return showFailure(pasynUser,"writeRaw");}

static asynStatus readFail(void *drvPvt, asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason)
{ return showFailure(pasynUser,"read");}

static asynStatus readRawFail(void *drvPvt, asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason)
{ return showFailure(pasynUser,"readRaw");}

static asynStatus flushFail(void *drvPvt,asynUser *pasynUser)
{ return showFailure(pasynUser,"flush");}

static asynStatus registerInterruptUserFail(void *drvPvt,asynUser *pasynUser,
       interruptCallbackOctet callback, void *userPvt, void **registrarPvt)
{ return showFailure(pasynUser,"registerInterruptUser");}

static asynStatus cancelInterruptUserFail(void *registrarPvt, asynUser *pasynUser)
{ return showFailure(pasynUser,"cancelInterruptUser");}

static asynStatus setInputEosFail(void *drvPvt,asynUser *pasynUser,
                        const char *eos,int eoslen)
{ return showFailure(pasynUser,"setInputEos");}

static asynStatus getInputEosFail(void *drvPvt,asynUser *pasynUser,
                       char *eos, int eossize, int *eoslen)
{ return showFailure(pasynUser,"getInputEos");}

static asynStatus setOutputEosFail(void *drvPvt,asynUser *pasynUser,
                        const char *eos,int eoslen)
{ return showFailure(pasynUser,"setOutputEos");}

static asynStatus getOutputEosFail(void *drvPvt,asynUser *pasynUser,
                       char *eos, int eossize, int *eoslen)
{ return showFailure(pasynUser,"getOutputEos");}
