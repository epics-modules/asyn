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

typedef struct drvPvt {
    asynUser *pasynUser;
    deviceDriver *padeviceDriver;
    octetDriver *poctetDriver;
    void *poctetDriverPvt;
}drvPvt;
    
static int processModuleInit(const char *processModuleName,const char *deviceName);


/* octetDriver methods */
static int read(void *ppvt,asynUser *pasynUser,int addr,char *data,int maxchars);
static int write(void *ppvt,asynUser *pasynUser,
    int addr,const char *data,int numchars);
static asynStatus flush(void *ppvt,asynUser *pasynUser,int addr);
static asynStatus setTimeout(void *ppvt,asynUser *pasynUser,
    asynTimeoutType type,double timeout);
static asynStatus setEos(void *ppvt,asynUser *pasynUser,const char *eos,int eoslen);
static asynStatus installPeekHandler(void *ppvt,asynUser *pasynUser,peekHandler handler);
static asynStatus removePeekHandler(void *ppvt,asynUser *pasynUser);
static octetDriver octet = {
    read,write,flush,
    setTimeout, setEos,
    installPeekHandler, removePeekHandler
};

static driverInterface mydriverInterface[NUM_INTERFACES] = {
    {octetDriverType,&octet}
};

static int processModuleInit(const char *pmn,const char *dn)
{
    drvPvt *pdrvPvt;
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
    pdrvPvt = callocMustSucceed(1,sizeof(drvPvt),"processModuleInit");
    pdrvPvt->pasynUser = pasynUser = pasynQueueManager->createAsynUser(0,0,0);
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
        padeviceDriver[i].pdrvPvt = pdrvPvt;
    }
    pdrvPvt->padeviceDriver = padeviceDriver;
    poctetdeviceDriver = pasynQueueManager->findDriver(pasynUser,
        octetDriverType,0);
    if(!poctetdeviceDriver) {
	printf("%s %s\n",octetDriverType,pasynUser->errorMessage);
	return(0);
    }
    pdrvPvt->poctetDriver = (octetDriver *)
	poctetdeviceDriver->pdriverInterface->pinterface;
    pdrvPvt->poctetDriverPvt = poctetdeviceDriver->pdrvPvt;
    status = pasynQueueManager->registerProcessModule(
        processModuleName,deviceName,
        padeviceDriver,NUM_INTERFACES);
    if(status!=asynSuccess) {
        printf("processModuleInit registerDriver failed\n");
    }
    return(0);
}

/* octetDriver methods */
static int read(void *ppvt,asynUser *pasynUser,int addr,char *data,int maxchars)
{
    drvPvt *pdrvPvt = (drvPvt *)ppvt;
    int nchars;

    printf("entered processModule::read\n");
    nchars = pdrvPvt->poctetDriver->read(pdrvPvt->poctetDriverPvt,
        pasynUser,addr,data,maxchars);
    return(nchars);
}

static int write(void *ppvt,asynUser *pasynUser, int addr,const char *data,int numchars)
{
    drvPvt *pdrvPvt = (drvPvt *)ppvt;
    int nchars;

    printf("entered processModule::write\n");
    nchars = pdrvPvt->poctetDriver->write(pdrvPvt->poctetDriverPvt,
        pasynUser,addr,data,numchars);
    return(nchars);
}

static asynStatus flush(void *ppvt,asynUser *pasynUser,int addr)
{
    drvPvt *pdrvPvt = (drvPvt *)ppvt;
    asynStatus status;

    printf("entered processModule::flush\n");
    status = pdrvPvt->poctetDriver->flush(pdrvPvt->poctetDriverPvt,pasynUser,addr);
    return(status);
}

static asynStatus setTimeout(void *ppvt,asynUser *pasynUser,
                asynTimeoutType type,double timeout)
{
    drvPvt *pdrvPvt = (drvPvt *)ppvt;
    asynStatus status;

    printf("entered processModule::setTimeout\n");
    status = pdrvPvt->poctetDriver->setTimeout(pdrvPvt->poctetDriverPvt,
        pasynUser,type,timeout);
    return(status);
}

static asynStatus setEos(void *ppvt,asynUser *pasynUser,const char *eos,int eoslen)
{
    drvPvt *pdrvPvt = (drvPvt *)ppvt;
    asynStatus status;

    printf("entered processModule::setEos\n");
    status = pdrvPvt->poctetDriver->setEos(pdrvPvt->poctetDriverPvt,
        pasynUser,eos,eoslen);
    return(status);
}

static asynStatus installPeekHandler(void *ppvt,asynUser *pasynUser,peekHandler handler)
{
    drvPvt *pdrvPvt = (drvPvt *)ppvt;
    asynStatus status;

    printf("entered processModule::installPeekHandler\n");
    status = pdrvPvt->poctetDriver->installPeekHandler(pdrvPvt->poctetDriverPvt,
        pasynUser,handler);
    return(status);
}

static asynStatus removePeekHandler(void *ppvt,asynUser *pasynUser)
{
    drvPvt *pdrvPvt = (drvPvt *)ppvt;
    asynStatus status;

    printf("entered processModule::removePeekHandler\n");
    status = pdrvPvt->poctetDriver->removePeekHandler(pdrvPvt->poctetDriverPvt,
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
