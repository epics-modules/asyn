/*processModule.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* gpibCore is distributed subject to a Software License Agreement
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
    deviceDriver *padeviceDriver;
    octetDriver *poctetDriver;
    void *octetDriverPvt;
}processPvt;
    
static int processModuleInit(const char *processModuleName,const char *deviceName);


/* octetDriver methods */
static int processRead(void *ppvt,asynUser *pasynUser,int addr,char *data,int maxchars);
static int processWrite(void *ppvt,asynUser *pasynUser,
    int addr,const char *data,int numchars);
static asynStatus processFlush(void *ppvt,asynUser *pasynUser,int addr);
static asynStatus setEos(void *ppvt,asynUser *pasynUser,
    int addr,const char *eos,int eoslen);
static asynStatus installPeekHandler(void *ppvt,asynUser *pasynUser,
    peekHandler handler,void *peekHandlerPvt);
static asynStatus removePeekHandler(void *ppvt,asynUser *pasynUser);
static octetDriver octet = {
    processRead,processWrite,processFlush,
    setEos,
    installPeekHandler, removePeekHandler
};

static driverInterface mydriverInterface[NUM_INTERFACES] = {
    {octetDriverType,&octet}
};

static int processModuleInit(const char *pmn,const char *dn)
{
    processPvt *pprocessPvt;
    char *processModuleName;
    char *deviceName;
    asynStatus status;
    deviceDriver *padeviceDriver;
    deviceDriver *poctetdeviceDriver;
    asynUser *pasynUser;
    int i;

    processModuleName = callocMustSucceed(strlen(pmn)+1,sizeof(char),
        "processModuleInit");
    strcpy(processModuleName,pmn);
    deviceName = callocMustSucceed(strlen(dn)+1,sizeof(char),
        "processModuleInit");
    strcpy(deviceName,dn);
    pprocessPvt = callocMustSucceed(1,sizeof(processPvt),"processModuleInit");
    pprocessPvt->pasynUser = pasynUser = pasynQueueManager->createAsynUser(0,0,0);
    status = pasynQueueManager->connectDevice(pasynUser,deviceName);
    if(status!=asynSuccess) {
        printf("processModuleInit connectDevice failed %s\n",
            pasynUser->errorMessage);
	return(0);
    }
    padeviceDriver = callocMustSucceed(NUM_INTERFACES,sizeof(deviceDriver),
        "processModuleInit");
    for(i=0; i<NUM_INTERFACES; i++) {
        padeviceDriver[i].pdriverInterface = &mydriverInterface[i];
        padeviceDriver[i].drvPvt = pprocessPvt;
    }
    pprocessPvt->padeviceDriver = padeviceDriver;
    poctetdeviceDriver = pasynQueueManager->findDriver(pasynUser,
        octetDriverType,0);
    if(!poctetdeviceDriver) {
	printf("%s %s\n",octetDriverType,pasynUser->errorMessage);
	return(0);
    }
    pprocessPvt->poctetDriver = (octetDriver *)
	poctetdeviceDriver->pdriverInterface->pinterface;
    pprocessPvt->octetDriverPvt = poctetdeviceDriver->drvPvt;
    status = pasynQueueManager->registerProcessModule(
        processModuleName,deviceName,
        padeviceDriver,NUM_INTERFACES);
    if(status!=asynSuccess) {
        printf("processModuleInit registerDriver failed\n");
    }
    return(0);
}

/* octetDriver methods */
static int processRead(void *ppvt,asynUser *pasynUser,int addr,char *data,int maxchars)
{
    processPvt *pprocessPvt = (processPvt *)ppvt;
    int nchars;

    printf("entered processModule::read\n");
    nchars = pprocessPvt->poctetDriver->read(pprocessPvt->octetDriverPvt,
        pasynUser,addr,data,maxchars);
    return(nchars);
}

static int processWrite(void *ppvt,asynUser *pasynUser, int addr,const char *data,int numchars)
{
    processPvt *pprocessPvt = (processPvt *)ppvt;
    int nchars;

    printf("entered processModule::write\n");
    nchars = pprocessPvt->poctetDriver->write(pprocessPvt->octetDriverPvt,
        pasynUser,addr,data,numchars);
    return(nchars);
}

static asynStatus processFlush(void *ppvt,asynUser *pasynUser,int addr)
{
    processPvt *pprocessPvt = (processPvt *)ppvt;
    asynStatus status;

    printf("entered processModule::flush\n");
    status = pprocessPvt->poctetDriver->flush(pprocessPvt->octetDriverPvt,pasynUser,addr);
    return(status);
}

static asynStatus setEos(void *ppvt,asynUser *pasynUser,
    int addr, const char *eos,int eoslen)
{
    processPvt *pprocessPvt = (processPvt *)ppvt;
    asynStatus status;

    printf("entered processModule::setEos\n");
    status = pprocessPvt->poctetDriver->setEos(pprocessPvt->octetDriverPvt,
        pasynUser,addr,eos,eoslen);
    return(status);
}

static asynStatus installPeekHandler(void *ppvt,asynUser *pasynUser,
    peekHandler handler,void *peekHandlerPvt)
{
    processPvt *pprocessPvt = (processPvt *)ppvt;
    asynStatus status;

    printf("entered processModule::installPeekHandler\n");
    status = pprocessPvt->poctetDriver->installPeekHandler(
        pprocessPvt->octetDriverPvt,pasynUser,handler,peekHandlerPvt);
    return(status);
}

static asynStatus removePeekHandler(void *ppvt,asynUser *pasynUser)
{
    processPvt *pprocessPvt = (processPvt *)ppvt;
    asynStatus status;

    printf("entered processModule::removePeekHandler\n");
    status = pprocessPvt->poctetDriver->removePeekHandler(pprocessPvt->octetDriverPvt,
        pasynUser);
    return(status);
}

/* register processModuleInit*/
static const iocshArg processModuleInitArg0 = { "processModuleName", iocshArgString };
static const iocshArg processModuleInitArg1 = { "deviceName", iocshArgString };
static const iocshArg *processModuleInitArgs[] = {
    &processModuleInitArg0,&processModuleInitArg1};
static const iocshFuncDef processModuleInitFuncDef = {
    "processModuleInit", 2, processModuleInitArgs};
static void processModuleInitCallFunc(const iocshArgBuf *args)
{
    processModuleInit(args[0].sval,args[1].sval);
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
