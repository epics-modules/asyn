/*asynInterposeFlush.c */
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
#include <epicsString.h>
#include <epicsExport.h>
#include <iocsh.h>

#include <asynDriver.h>

#define epicsExportSharedSymbols

#include "asynShellCommands.h"
#include "asynInterposeFlush.h"

typedef struct interposePvt {
    char          *portName;
    int           addr;
    asynInterface octet;
    asynOctet     *pasynOctetDrv;
    void          *drvPvt;
    double        timeout;
}interposePvt;
    
/* asynOctet methods */
static asynStatus processRead(void *ppvt,asynUser *pasynUser,
    char *data,int maxchars,int *nbytesTransfered);
static asynStatus processWrite(void *ppvt,asynUser *pasynUser,
    const char *data,int numchars,int *nbytesTransfered);
static asynStatus processFlush(void *ppvt,asynUser *pasynUser);
static asynStatus setEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynStatus getEos(void *ppvt,asynUser *pasynUser,
    char *eos,int eossize,int *eoslen);
static asynOctet octet = {
    processRead,processWrite,processFlush,setEos,getEos
};

int epicsShareAPI
asynInterposeFlushConfig(const char *portName,int addr,int timeout)
{
    interposePvt *pinterposePvt;
    asynStatus status;
    asynInterface *poctetasynInterface;

    pinterposePvt = callocMustSucceed(1,sizeof(interposePvt),"interposeInterfaceInit");
    pinterposePvt->portName = epicsStrDup(portName);
    pinterposePvt->addr = addr;
    pinterposePvt->octet.interfaceType = asynOctetType;
    pinterposePvt->octet.pinterface = &octet;
    pinterposePvt->octet.drvPvt = pinterposePvt;
    if(timeout<=0) timeout = 1;
    pinterposePvt->timeout = ((double)timeout)*.001;
    status = pasynManager->interposeInterface(portName,addr,
        &pinterposePvt->octet,&poctetasynInterface);
    if((status!=asynSuccess) || !poctetasynInterface) {
	printf("%s interposeInterface failed.\n",portName);
        free((void *)pinterposePvt->portName);
        free(pinterposePvt);
        return(-1);
    }
    pinterposePvt->pasynOctetDrv = (asynOctet *)poctetasynInterface->pinterface;
    pinterposePvt->drvPvt = poctetasynInterface->drvPvt;
    return(0);
}

/* asynOctet methods */
static asynStatus processRead(void *ppvt,asynUser *pasynUser,
    char *data,int maxchars,int *nbytesTransfered)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    return pinterposePvt->pasynOctetDrv->read(pinterposePvt->drvPvt,
        pasynUser,data,maxchars,nbytesTransfered);
}

static asynStatus processWrite(void *ppvt,asynUser *pasynUser,
    const char *data,int numchars,int *nbytesTransfered)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    return pinterposePvt->pasynOctetDrv->write(pinterposePvt->drvPvt,
        pasynUser,data,numchars,nbytesTransfered);
}

static asynStatus processFlush(void *ppvt,asynUser *pasynUser)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;
    asynOctet    *pasynOctetDrv = pinterposePvt->pasynOctetDrv;
    void         *drvPvt = pinterposePvt->drvPvt;
    double       savetimeout = pasynUser->timeout;
    char         buffer[100];
    asynStatus   status;
    int          nbytesTransfered;

    asynPrint(pasynUser,ASYN_TRACEIO_FILTER,"entered asynInterposeFlush::flush\n");
    pasynUser->timeout = pinterposePvt->timeout;
    while(1) {
        nbytesTransfered = 0;
        status = pasynOctetDrv->read(drvPvt,pasynUser,
            buffer,sizeof(buffer),&nbytesTransfered);
        if(nbytesTransfered==0) break;
        asynPrintIO(pasynUser,ASYN_TRACEIO_FILTER,
            buffer,nbytesTransfered,"asynInterposeFlush:flush ");
    }
    pasynUser->timeout = savetimeout;
    return(asynSuccess);
}

static asynStatus setEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    return pinterposePvt->pasynOctetDrv->setEos(pinterposePvt->drvPvt,
        pasynUser,eos,eoslen);
}

static asynStatus getEos(void *ppvt,asynUser *pasynUser,
    char *eos,int eossize,int *eoslen)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    return pinterposePvt->pasynOctetDrv->getEos(pinterposePvt->drvPvt,
        pasynUser,eos,eossize,eoslen);
}

/* register asynInterposeFlushConfig*/
static const iocshArg asynInterposeFlushConfigArg0 =
    { "portName", iocshArgString };
static const iocshArg asynInterposeFlushConfigArg1 =
    { "addr", iocshArgInt };
static const iocshArg asynInterposeFlushConfigArg2 =
    { "timeout", iocshArgDouble };
static const iocshArg *asynInterposeFlushConfigArgs[] = 
    {&asynInterposeFlushConfigArg0,&asynInterposeFlushConfigArg1,
    &asynInterposeFlushConfigArg2};
static const iocshFuncDef asynInterposeFlushConfigFuncDef =
    {"asynInterposeFlushConfig", 3, asynInterposeFlushConfigArgs};
static void asynInterposeFlushConfigCallFunc(const iocshArgBuf *args)
{
    asynInterposeFlushConfig(args[0].sval,args[1].ival,args[2].dval);
}

static void asynInterposeFlushRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&asynInterposeFlushConfigFuncDef, asynInterposeFlushConfigCallFunc);
    }
}
epicsExportRegistrar(asynInterposeFlushRegister);
