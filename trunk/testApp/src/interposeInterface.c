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
#include <epicsExport.h>
#include <iocsh.h>

#include <asynDriver.h>

#define NUM_INTERFACES 1

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
static int processRead(void *ppvt,asynUser *pasynUser,char *data,int maxchars);
static int processWrite(void *ppvt,asynUser *pasynUser,
    const char *data,int numchars);
static asynStatus processFlush(void *ppvt,asynUser *pasynUser);
static asynStatus setEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynOctet octet = {
    processRead,processWrite,processFlush, setEos
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
static int processRead(void *ppvt,asynUser *pasynUser,char *data,int maxchars)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;
    int nchars;

    asynPrint(pasynUser,ASYN_TRACEIO_FILTER,"entered interposeInterface::read\n");
    nchars = pinterposePvt->pasynOctet->read(pinterposePvt->asynOctetPvt,
        pasynUser,data,maxchars);
    return(nchars);
}

static int processWrite(void *ppvt,asynUser *pasynUser,const char *data,int numchars)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;
    int nchars;

    asynPrint(pasynUser,ASYN_TRACEIO_FILTER,"entered interposeInterface::write\n");
    nchars = pinterposePvt->pasynOctet->write(pinterposePvt->asynOctetPvt,
        pasynUser,data,numchars);
    return(nchars);
}

static asynStatus processFlush(void *ppvt,asynUser *pasynUser)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;
    asynStatus status;

    asynPrint(pasynUser,ASYN_TRACEIO_FILTER,"entered interposeInterface::flush\n");
    status = pinterposePvt->pasynOctet->flush(pinterposePvt->asynOctetPvt,pasynUser);
    return(status);
}

static asynStatus setEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;
    asynStatus status;

    asynPrint(pasynUser,ASYN_TRACEIO_FILTER,"entered interposeInterface::setEos\n");
    status = pinterposePvt->pasynOctet->setEos(pinterposePvt->asynOctetPvt,
        pasynUser,eos,eoslen);
    return(status);
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
