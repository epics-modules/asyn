/*interposeFlush.c */
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
#include <epicsExport.h>
#include <iocsh.h>

#include <asynDriver.h>

#define epicsExportSharedSymbols

#include "asynShellCommands.h"

typedef struct interposePvt {
    const char    *portName;
    int           addr;
    asynInterface octet;
    asynOctet     *pasynOctet;
    void          *asynOctetPvt;
    double        timeout;
}interposePvt;
    
int epicsShareAPI interposeFlushConfig(const char *portName,int addr,double timeout);


/* asynOctet methods */
static int processRead(void *ppvt,asynUser *pasynUser,char *data,int maxchars);
static int processWrite(void *ppvt,asynUser *pasynUser,
    const char *data,int numchars);
static asynStatus processFlush(void *ppvt,asynUser *pasynUser);
static asynStatus setEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynStatus setEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eossize,int *eoslen);
static asynOctet octet = {
    processRead,processWrite,processFlush,setEos,getEos
};

int epicsShareAPI
interposeFlushConfig(const char *portName,int addr,double timeout)
{
    interposePvt *pinterposePvt;
    asynStatus status;
    asynInterface *poctetasynInterface;

    portName = epicsStrDup(dn);
    pinterposePvt = callocMustSucceed(1,sizeof(interposePvt),"interposeInterfaceInit");
    pinterposePvt->portName = epicsStrDup(portName);
    pinterposePvt->addr = addr;
    pinterposePvt->octet.interfaceType = asynOctetType;
    pinterposePvt->octet.pinterface = &octet;
    pinterposePvt->octet.drvPvt = pinterposePvt;
    pinterposePvt->timeout = timeout;
    status = pasynManager->interposeInterface(portName,addr,
        &pinterposePvt->octet,&poctetasynInterface);
    if((status!=asynSuccess) || !poctetasynInterface) {
	printf("%s interposeInterface failed.\n",portName);
        free(pinterposePvt->portName);
        free(pinterposePvt);
        return(-1);
    }
    pinterposePvt->pasynOctet = (asynOctet *)poctetasynInterface->pinterface;
    pinterposePvt->asynOctetPvt = poctetasynInterface->drvPvt;
    return(0);
}

/* asynOctet methods */
static int processRead(void *ppvt,asynUser *pasynUser,char *data,int maxchars)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    return pinterposePvt->pasynOctet->read(pinterposePvt->asynOctetPvt,
        pasynUser,data,maxchars);
}

static int processWrite(void *ppvt,asynUser *pasynUser,const char *data,int numchars)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    return pinterposePvt->pasynOctet->write(pinterposePvt->asynOctetPvt,
        pasynUser,data,numchars);
}

static asynStatus processFlush(void *ppvt,asynUser *pasynUser)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;
    asynOctet    *pasynOctet = pinterposePvt->pasynOctet;
    void         *drvPvt = pinterposePvt->asynOctetPvt;
    double       savetimeout = pasynUser->timeout;
    char         buffer[100];
    int          nin;

    asynPrint(pasynUser,ASYN_TRACEIO_FILTER,"entered interposeFlush::flush\n");
    pasynUser->timeout = pinterposePvt->timeout;
    while(1) {
        nin = pasynOctet->read(drvPvt,pasynUser,buffer,sizeof(buffer));
        if(nin<=0) break;
        asynPrintIO(pasynUser,ASYN_TRACEIO_FILTER,
            buffer,nin,"interposeFlush:flush ");
    }
    pasynUser->timeout = savetimeout;
    return(asynSuccess);
}

static asynStatus setEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    return pinterposePvt->pasynOctet->setEos(pinterposePvt->asynOctetPvt,
        pasynUser,eos,eoslen);
}

static asynStatus getEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eossize,int *eoslen)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    return pinterposePvt->pasynOctet->getEos(pinterposePvt->asynOctetPvt,
        pasynUser,eos,eossize,eoslen);
}

/* register interposeFlushConfig*/
static const iocshArg interposeFlushConfigArg0 =
    { "portName", iocshArgString };
static const iocshArg interposeFlushConfigArg1 =
    { "addr", iocshArgInt };
static const iocshArg interposeFlushConfigArg2 =
    { "timeout", iocshArgDouble };
static const iocshArg *interposeFlushConfigArgs[] = 
    {&interposeFlushConfigArg0,&interposeFlushConfigArg1,
    &interposeFlushConfigArg2};
static const iocshFuncDef interposeFlushConfigFuncDef =
    {"interposeFlushConfig", 3, interposeFlushConfigArgs};
static void interposeFlushConfigCallFunc(const iocshArgBuf *args)
{
    interposeFlushConfig(args[0].sval,args[1].ival,args[2].dval);
}

static void interposeFlushRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&interposeFlushConfigFuncDef, interposeFlushConfigCallFunc);
    }
}
epicsExportRegistrar(interposeFlushRegister);
