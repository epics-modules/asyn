/*interposeInterface.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*test process module for asyn support*/
/* 
 * Author: Marty Kraimer
 */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <cantProceed.h>
#include <epicsStdio.h>
#include <iocsh.h>

#include <asynDriver.h>
#include <asynOctet.h>

#define NUM_INTERFACES 1

#include <epicsExport.h>
typedef struct interposePvt {
    const char *interposeName;
    const char *portName;
    int        addr;
    asynInterface octet;
    asynOctet *pasynOctet;
    void *asynOctetPvt;
}interposePvt;
    
static int interposeInterfaceInit(const char *interposeInterfaceName,
    const char *portName,int addr);


/* asynOctet methods */
static asynStatus writeIt(void *ppvt,asynUser *pasynUser,
    const char *data,size_t numchars,size_t *nbytesTransfered);
static asynStatus readIt(void *ppvt,asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason);
static asynStatus flushIt(void *ppvt,asynUser *pasynUser);
static asynStatus registerInterruptUser(void *ppvt,asynUser *pasynUser,
    interruptCallbackOctet callback, void *userPvt,void **registrarPvt);
static asynStatus cancelInterruptUser(void *ppvt,asynUser *pasynUser,
    void *registrarPvt);
static asynStatus setInputEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynStatus getInputEos(void *ppvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen);
static asynStatus setOutputEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynStatus getOutputEos(void *ppvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen);
static asynOctet octet = {
    writeIt,readIt,flushIt,
    registerInterruptUser,cancelInterruptUser,
    setInputEos,getInputEos,setOutputEos,getOutputEos
};

static int interposeInterfaceInit(const char *pmn,const char *dn,int addr)
{
    interposePvt *pinterposePvt;
    char *interposeName;
    char *portName;
    asynStatus status;
    asynInterface *poctetasynInterface;

    interposeName = callocMustSucceed(strlen(pmn)+1,sizeof(char),
        "interposeInterfaceInit");
    strcpy(interposeName,pmn);
    portName = callocMustSucceed(strlen(dn)+1,sizeof(char),
        "interposeInterfaceInit");
    strcpy(portName,dn);
    pinterposePvt = callocMustSucceed(1,sizeof(interposePvt),"interposeInterfaceInit");
    pinterposePvt->interposeName = interposeName;
    pinterposePvt->portName = portName;
    pinterposePvt->addr = addr;
    pinterposePvt->octet.interfaceType = asynOctetType;
    pinterposePvt->octet.pinterface = &octet;
    pinterposePvt->octet.drvPvt = pinterposePvt;
    status = pasynManager->interposeInterface(portName,addr,
        &pinterposePvt->octet,&poctetasynInterface);
    if((status!=asynSuccess) || !poctetasynInterface) {
        printf("%s interposeInterface failed.\n",portName);
        free(pinterposePvt);
        free(portName);
        free(interposeName);
        return(0);
    }
    pinterposePvt->pasynOctet = (asynOctet *)poctetasynInterface->pinterface;
    pinterposePvt->asynOctetPvt = poctetasynInterface->drvPvt;
    return(0);
}

/* asynOctet methods */
static asynStatus writeIt(void *ppvt,asynUser *pasynUser,
    const char *data,size_t numchars,size_t *nbytesTransfered)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    asynPrint(pasynUser,ASYN_TRACEIO_FILTER,
        "entered interposeInterface::write\n");
    return pinterposePvt->pasynOctet->write(pinterposePvt->asynOctetPvt,
        pasynUser,data,numchars,nbytesTransfered);
}

static asynStatus readIt(void *ppvt,asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    asynPrint(pasynUser,ASYN_TRACEIO_FILTER,
        "entered interposeInterface::read\n");
    return pinterposePvt->pasynOctet->read(pinterposePvt->asynOctetPvt,
        pasynUser,data,maxchars,nbytesTransfered,eomReason);
}

static asynStatus flushIt(void *ppvt,asynUser *pasynUser)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    asynPrint(pasynUser,ASYN_TRACEIO_FILTER,
        "entered interposeInterface::flush\n");
    return pinterposePvt->pasynOctet->flush(
        pinterposePvt->asynOctetPvt,pasynUser);
}

static asynStatus registerInterruptUser(void *ppvt,asynUser *pasynUser,
    interruptCallbackOctet callback, void *userPvt,void **registrarPvt)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    asynPrint(pasynUser,ASYN_TRACEIO_FILTER,
        "entered interposeInterface::registerInterruptUser\n");
    return pinterposePvt->pasynOctet->registerInterruptUser(
        pinterposePvt->asynOctetPvt,pasynUser,callback,userPvt,registrarPvt);
}

static asynStatus cancelInterruptUser(void *drvPvt,asynUser *pasynUser,
    void *registrarPvt)
{
    interposePvt *pinterposePvt = (interposePvt *)drvPvt;

    asynPrint(pasynUser,ASYN_TRACEIO_FILTER,
        "entered interposeInterface::cancelInterruptUser\n");
    return pinterposePvt->pasynOctet->cancelInterruptUser(
        pinterposePvt->asynOctetPvt,pasynUser,registrarPvt);
}

static asynStatus setInputEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    asynPrint(pasynUser,ASYN_TRACEIO_FILTER,
        "entered interposeInterface::setInputEos\n");
    return pinterposePvt->pasynOctet->setInputEos(pinterposePvt->asynOctetPvt,
        pasynUser,eos,eoslen);
}

static asynStatus getInputEos(void *ppvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    asynPrint(pasynUser,ASYN_TRACEIO_FILTER,
        "entered interposeInterface::getInputEos\n");
    return pinterposePvt->pasynOctet->getInputEos(pinterposePvt->asynOctetPvt,
        pasynUser,eos,eossize,eoslen);
}

static asynStatus setOutputEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    asynPrint(pasynUser,ASYN_TRACEIO_FILTER,
        "entered interposeInterface::setOutputEos\n");
    return pinterposePvt->pasynOctet->setOutputEos(pinterposePvt->asynOctetPvt,
        pasynUser,eos,eoslen);
}

static asynStatus getOutputEos(void *ppvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    asynPrint(pasynUser,ASYN_TRACEIO_FILTER,
        "entered interposeInterface::getOutputEos\n");
    return pinterposePvt->pasynOctet->getOutputEos(pinterposePvt->asynOctetPvt,
        pasynUser,eos,eossize,eoslen);
}

/* register interposeInterfaceInit*/
static const iocshArg interposeInterfaceInitArg0 =
    {"interposeInterfaceName", iocshArgString };
static const iocshArg interposeInterfaceInitArg1 =
    { "portName", iocshArgString };
static const iocshArg interposeInterfaceInitArg2 =
    { "addr", iocshArgInt };
static const iocshArg *interposeInterfaceInitArgs[] = 
    {&interposeInterfaceInitArg0,&interposeInterfaceInitArg1,
    &interposeInterfaceInitArg2};
static const iocshFuncDef interposeInterfaceInitFuncDef =
    {"interposeInterfaceInit", 3, interposeInterfaceInitArgs};
static void interposeInterfaceInitCallFunc(const iocshArgBuf *args)
{
    interposeInterfaceInit(args[0].sval,args[1].sval,args[2].ival);
}

static void interposeInterfaceRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&interposeInterfaceInitFuncDef, interposeInterfaceInitCallFunc);
    }
}
epicsExportRegistrar(interposeInterfaceRegister);
