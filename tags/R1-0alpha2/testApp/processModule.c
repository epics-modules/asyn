/*processModule.c */
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

typedef struct processPvt {
    asynUser *pasynUser;
    asynInterface *paasynInterface;
    asynOctet *pasynOctet;
    void *asynOctetPvt;
}processPvt;
    
static int processModuleInit(const char *processModuleName,
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

static int processModuleInit(const char *pmn,const char *dn,int addr)
{
    processPvt *pprocessPvt;
    char *processModuleName;
    char *portName;
    asynStatus status;
    asynInterface *paasynInterface;
    asynInterface *poctetasynInterface;
    asynUser *pasynUser;

    processModuleName = callocMustSucceed(strlen(pmn)+1,sizeof(char),
        "processModuleInit");
    strcpy(processModuleName,pmn);
    portName = callocMustSucceed(strlen(dn)+1,sizeof(char),
        "processModuleInit");
    strcpy(portName,dn);
    pprocessPvt = callocMustSucceed(1,sizeof(processPvt),"processModuleInit");
    pprocessPvt->pasynUser = pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if(status!=asynSuccess) {
        printf("processModuleInit connectPort failed %s\n",
            pasynUser->errorMessage);
	return(0);
    }
    paasynInterface = callocMustSucceed(NUM_INTERFACES,sizeof(asynInterface),
        "processModuleInit");
    paasynInterface[0].interfaceType = asynOctetType;
    paasynInterface[0].pinterface = &octet;
    paasynInterface[0].drvPvt = pprocessPvt;
    pprocessPvt->paasynInterface = paasynInterface;
    poctetasynInterface = pasynManager->findInterface(pasynUser,
        asynOctetType,0);
    if(!poctetasynInterface) {
	printf("%s %s\n",asynOctetType,pasynUser->errorMessage);
	return(0);
    }
    pprocessPvt->pasynOctet = (asynOctet *)
	poctetasynInterface->pinterface;
    pprocessPvt->asynOctetPvt = poctetasynInterface->drvPvt;
    status = pasynManager->registerProcessModule(
        processModuleName,portName,addr,
        paasynInterface,NUM_INTERFACES);
    if(status!=asynSuccess) {
        printf("processModuleInit registerDriver failed\n");
    }
    pasynManager->freeAsynUser(pasynUser);
    return(0);
}

/* asynOctet methods */
static int processRead(void *ppvt,asynUser *pasynUser,char *data,int maxchars)
{
    processPvt *pprocessPvt = (processPvt *)ppvt;
    int nchars;

    printf("entered processModule::read\n");
    nchars = pprocessPvt->pasynOctet->read(pprocessPvt->asynOctetPvt,
        pasynUser,data,maxchars);
    return(nchars);
}

static int processWrite(void *ppvt,asynUser *pasynUser,const char *data,int numchars)
{
    processPvt *pprocessPvt = (processPvt *)ppvt;
    int nchars;

    printf("entered processModule::write\n");
    nchars = pprocessPvt->pasynOctet->write(pprocessPvt->asynOctetPvt,
        pasynUser,data,numchars);
    return(nchars);
}

static asynStatus processFlush(void *ppvt,asynUser *pasynUser)
{
    processPvt *pprocessPvt = (processPvt *)ppvt;
    asynStatus status;

    printf("entered processModule::flush\n");
    status = pprocessPvt->pasynOctet->flush(pprocessPvt->asynOctetPvt,pasynUser);
    return(status);
}

static asynStatus setEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    processPvt *pprocessPvt = (processPvt *)ppvt;
    asynStatus status;

    printf("entered processModule::setEos\n");
    status = pprocessPvt->pasynOctet->setEos(pprocessPvt->asynOctetPvt,
        pasynUser,eos,eoslen);
    return(status);
}

/* register processModuleInit*/
static const iocshArg processModuleInitArg0 = {
    "processModuleName", iocshArgString };
static const iocshArg processModuleInitArg1 = { "portName", iocshArgString };
static const iocshArg processModuleInitArg2 = { "addr", iocshArgInt };
static const iocshArg *processModuleInitArgs[] = {
    &processModuleInitArg0,&processModuleInitArg1,&processModuleInitArg2};
static const iocshFuncDef processModuleInitFuncDef = {
    "processModuleInit", 3, processModuleInitArgs};
static void processModuleInitCallFunc(const iocshArgBuf *args)
{
    processModuleInit(args[0].sval,args[1].sval,args[2].ival);
}

static void processModuleRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&processModuleInitFuncDef, processModuleInitCallFunc);
    }
}
epicsExportRegistrar(processModuleRegister);
