/* devSupportGpib.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* gpibCore is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
 * Current Author: Marty Kraimer
 * Original Authors: John Winans and Benjamin Franksen
 *
 */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include <epicsAssert.h>
#include <dbAccess.h>
#include <recGbl.h>
#include <alarm.h>
#include <dbDefs.h>
#include <dbAccess.h>
#include <dbScan.h>
#include <devSup.h>
#include <recSup.h>
#include <link.h>
#include <epicsThread.h>
#include <epicsMutex.h>
#include <epicsTimer.h>
#include <epicsTime.h>
#include <cantProceed.h>
#include <epicsStdio.h>
#include <shareLib.h>
#include <epicsExport.h>
#include <iocsh.h>

#include <asynDriver.h>
#include <gpibDriver.h>

#define epicsExportSharedSymbols
#include "devSupportGpib.h"

#define DEFAULT_QUEUE_TIMEOUT 60.0
#define DEFAULT_SRQ_WAIT_TIMEOUT 60.0

typedef struct commonGpibPvt {
    ELLLIST deviceInterfaceList;
    epicsTimerQueueId timerQueue;
}commonGpibPvt;
static commonGpibPvt *pcommonGpibPvt=0;

typedef struct deviceInstance {
    ELLNODE node;
    ELLLIST waitList;
    int gpibAddr;
    unsigned long tmoCount;     /* total number of timeouts since boot time */
    unsigned long errorCount;   /* total number of errors since boot time */
    double queueTimeout;
    double srqWaitTimeout;
    /*Following fields are for timeWindow*/
    int timeoutActive;
    epicsTimeStamp timeoutTime;
    /*Following fields are for GPIBSRQHANDLER*/
    srqHandler unsollicitedHandler;
    void *userPrivate;
    /*Following fields are for GPIBREADW and GPIBEFASTIW*/
    epicsTimerId srqWaitTimer;  /*to wait for SRQ*/
    int waitForSRQ;
    gpibDpvt *pgpibDpvt;        /*for record waiting for SRQ*/
    int queueRequestFromSrq;
}deviceInstance;

typedef struct deviceInterface {
    ELLNODE node;
    ELLLIST deviceInstanceList;
    epicsMutexId lock; 
    int link;
    char *interfaceName;
    asynDriver *pasynDriver;
    void *pasynDriverPvt;
    octetDriver *poctetDriver;
    void *poctetDriverPvt;
    gpibDriverUser *pgpibDriverUser;
    void *pgpibDriverUserPvt;
    void *pupvt;                /* user defined pointer */
}deviceInterface;

struct devGpibPvt {
    deviceInterface *pdeviceInterface;
    deviceInstance *pdeviceInstance;
    gpibWork work;
    gpibWork finish;
    char eos[2]; /*If GPIBEOS is specified*/
    /*Following are used in respond2Writes is true*/
};

static long initRecord(dbCommon* precord, struct link * plink);
static long processGPIBSOFT(gpibDpvt *pgpibDpvt);
static void queueReadRequest(gpibDpvt *pgpibDpvt, gpibWork finish);
static void queueWriteRequest(gpibDpvt *pgpibDpvt, gpibWork finish);
static void queueRequest(gpibDpvt *pgpibDpvt, gpibWork work);
static void report(int level);
static void registerSrqHandler(gpibDpvt *pgpibDpvt,
    srqHandler handler,void *userPrivate);
static int writeMsgLong(gpibDpvt *pgpibDpvt,long val);
static int writeMsgULong(gpibDpvt *pgpibDpvt,unsigned long val);
static int writeMsgDouble(gpibDpvt *pgpibDpvt,double val);
static int writeMsgString(gpibDpvt *pgpibDpvt,const char *str);

static devSupportGpib gpibSupport = {
    initRecord,
    processGPIBSOFT,
    queueReadRequest,
    queueWriteRequest,
    queueRequest,
    report,
    registerSrqHandler,
    writeMsgLong,
    writeMsgULong,
    writeMsgDouble,
    writeMsgString
};
epicsShareDef devSupportGpib *pdevSupportGpib = &gpibSupport;

/*Initialization routines*/
static void commonGpibPvtInit(void);
static deviceInterface *createDeviceInteface(
    int link,asynUser *pasynUser,const char *interfaceName);
static int getDeviceInterface(gpibDpvt *pgpibDpvt,int link);

/*Process routines */
static void queueIt(gpibDpvt *pgpibDpvt);
static void gpibRead(gpibDpvt *pgpibDpvt,int timeoutOccured);
static void gpibReadWaitComplete(gpibDpvt *pgpibDpvt,int timeoutOccured);
static void gpibWrite(gpibDpvt *pgpibDpvt,int timeoutOccured);

/*Callback routines*/
static void queueCallback(void *puserPvt);
static void timeoutCallback(void *puserPvt);
void srqHandlerGpib(void *parm, int gpibAddr, int statusByte);
void srqWaitTimeout(void *parm);

/*Utility routines*/
static int checkEnums(char * msg, char **enums);
static int waitForSRQClear(gpibDpvt *pgpibDpvt,
    deviceInterface *pdeviceInterface,deviceInstance *pdeviceInstance);
static int writeIt(gpibDpvt *pgpibDpvt,char *message,int len);

/*iocsh routines */
static void devGpibQueueTimeoutSet(
    const char *interfaceName, int gpibAddr, double timeout);
static void devGpibSrqWaitTimeoutSet(
    const char *interfaceName, int gpibAddr, double timeout);

static long initRecord(dbCommon *precord, struct link *plink)
{
    gDset *pgDset = (gDset *)precord->dset;
    devGpibParmBlock *pdevGpibParmBlock = pgDset->pdevGpibParmBlock;
    gpibDpvt *pgpibDpvt;
    devGpibPvt *pdevGpibPvt;
    gpibCmd *pgpibCmd;
    deviceInterface *pdeviceInterface;
    asynUser *pasynUser;
    int link,gpibAddr,parm;
    asynStatus status;
    asynDriver *pasynDriver;
    octetDriver *poctetDriver;
    

    if(plink->type==GPIB_IO) {
	link = plink->value.gpibio.link;
	gpibAddr = plink->value.gpibio.addr;
	sscanf(plink->value.gpibio.parm, "%d", &parm);
    } else if(plink->type==BBGPIB_IO) {
	link = ((plink->value.bbgpibio.link + 1) << 8)
	       + plink->value.bbgpibio.bbaddr;
	gpibAddr = plink->value.bbgpibio.gpibaddr;
	sscanf(plink->value.bbgpibio.parm, "%d", &parm);
    } else {
	printf("%s: init_record : GPIB link type %d is invalid",
            precord->name,plink->type);
	precord->pact = TRUE;	/* keep record from being processed */
	return (0);
    }
    pgpibDpvt = (gpibDpvt *) callocMustSucceed(1,
	    (sizeof(gpibDpvt)+ sizeof(devGpibPvt)),"devSupportGpib");
    precord->dpvt = pgpibDpvt;
    pdevGpibPvt = (devGpibPvt *)(pgpibDpvt + 1);
    pgpibDpvt->pdevGpibPvt = pdevGpibPvt;
    pasynUser = pasynQueueManager->createAsynUser(
        queueCallback,timeoutCallback,precord->dpvt);
    pasynUser->timeout = pdevGpibParmBlock->timeout;
    pgpibDpvt->pasynUser = pasynUser;
    pgpibDpvt->pdevGpibParmBlock = pdevGpibParmBlock;
    pgpibDpvt->gpibAddr = gpibAddr;
    pgpibDpvt->precord = precord;
    pgpibDpvt->parm = parm;
    if(getDeviceInterface(pgpibDpvt,link)) {
        printf("%s: init_record : no driver for link %d\n",precord->name,link);
        precord->pact = TRUE;	/* keep record from being processed */
	free(pasynUser);
        return(0);
    }
    pgpibCmd = gpibCmdGet(pgpibDpvt);
    pdeviceInterface = pdevGpibPvt->pdeviceInterface;
    pasynDriver = pdeviceInterface->pasynDriver;
    poctetDriver = pdeviceInterface->poctetDriver;
    if(!pasynDriver || !poctetDriver) {
        printf("%s: init_record : pasynDriver %p poctetDriver %p\n",
           precord->name,pgpibDpvt->pasynDriver,pgpibDpvt->poctetDriver);
        precord->pact = TRUE;	/* keep record from being processed */
	free(pasynUser);
        return(0);
    }
    pgpibDpvt->pasynDriver = pasynDriver;
    pgpibDpvt->pasynDriverPvt = pdeviceInterface->pasynDriverPvt;
    pgpibDpvt->poctetDriver = poctetDriver;
    pgpibDpvt->poctetDriverPvt = pdeviceInterface->poctetDriverPvt;
    if(pgpibCmd->type&GPIBEOS) {
        pdevGpibPvt->eos[0] = (char)pgpibCmd->eosChar;
        status = pgpibDpvt->poctetDriver->setEos(
            pgpibDpvt->poctetDriverPvt,pgpibDpvt->pasynUser,
            pdevGpibPvt->eos,1);
        if(status!=asynSuccess) {
            printf("%s poctetDriver->setEos failed %s\n",
                precord->name,pgpibDpvt->pasynUser->errorMessage);
        }
    }
    pgpibDpvt->pgpibDriverUser = pdeviceInterface->pgpibDriverUser;
    pgpibDpvt->pgpibDriverUserPvt = pdeviceInterface->pgpibDriverUserPvt;
    if (pgpibCmd->msgLen > 0) {
	pgpibDpvt->msg = (char *)callocMustSucceed(
            pgpibCmd->msgLen,sizeof(char),"devSupportGpib");
    }
    if (pgpibCmd->rspLen > 0) {
	pgpibDpvt->rsp = (char *)callocMustSucceed(
            pgpibCmd->rspLen,sizeof(char),"devSupportGpib");
    }
    if (pgpibCmd->dset != (gDset *) precord->dset) {
        printf("%s : init_record : record type invalid for spec'd "
            "GPIB param#%d\n", precord->name,pgpibDpvt->parm);
	precord->pact = TRUE;	/* keep record from being processed */
    }
    return (0);
}

static long processGPIBSOFT(gpibDpvt *pgpibDpvt)
{
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);

    return(pgpibCmd->convert(pgpibDpvt,pgpibCmd->P1,pgpibCmd->P2,pgpibCmd->P3));
}

static void queueReadRequest(gpibDpvt *pgpibDpvt, gpibWork finish)
{
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    dbCommon *precord = pgpibDpvt->precord;
    asynStatus status;

    if(pgpibDpvt->pdevGpibParmBlock->debugFlag)
        printf("%s queueReadRequest\n",precord->name);
    pdevGpibPvt->work = gpibRead;
    pdevGpibPvt->finish = finish;
    status = pasynQueueManager->lock(pgpibDpvt->pasynUser);
    if(status!=asynSuccess) {
        printf("%s pasynQueueManager->lock failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
        recGblSetSevr(precord, SOFT_ALARM, INVALID_ALARM);
        return;
    }
    queueIt(pgpibDpvt);
}

static void queueWriteRequest(gpibDpvt *pgpibDpvt, gpibWork finish)
{
    dbCommon *precord = pgpibDpvt->precord;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;

    if(pgpibDpvt->pdevGpibParmBlock->debugFlag)
        printf("%s queueWriteRequest\n",precord->name);
    pdevGpibPvt->work = gpibWrite;
    pdevGpibPvt->finish = finish;
    queueIt(pgpibDpvt);
}
static void queueRequest(gpibDpvt *pgpibDpvt, gpibWork work)
{
    dbCommon *precord = pgpibDpvt->precord;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;

    if(pgpibDpvt->pdevGpibParmBlock->debugFlag)
        printf("%s queueRequest\n",precord->name);
    pdevGpibPvt->work = work;
    pdevGpibPvt->finish = 0;
    queueIt(pgpibDpvt);
}

static void report(int interest)
{
    asynUser *pasynUser;
    deviceInterface  *pdeviceInterface;
    deviceInstance *pdeviceInstance;

    if(!pcommonGpibPvt) commonGpibPvtInit();
    pasynUser = pasynQueueManager->createAsynUser(0,0,0);
    pdeviceInterface = (deviceInterface *)ellFirst(
        &pcommonGpibPvt->deviceInterfaceList);
    while(pdeviceInterface) {
        printf("link %d interfaceName %s\n",
            pdeviceInterface->link,pdeviceInterface->interfaceName);
        printf("    pasynDriver %p poctetDriver %p pgpibDriverUser %p\n",
            pdeviceInterface->pasynDriver,pdeviceInterface->poctetDriver,
            pdeviceInterface->pgpibDriverUser);
        if(pdeviceInterface->pasynDriver) {
            pdeviceInterface->pasynDriver->report(
                pdeviceInterface->pasynDriverPvt,pasynUser,interest);
        }
        pdeviceInstance = (deviceInstance *)ellFirst(
            &pdeviceInterface->deviceInstanceList);
        while(pdeviceInstance) {
            printf("    gpibAddr %d timeouts %lu errors %lu "
		   "queueTimeout %f srqWaitTimeout %f\n",
                pdeviceInstance->gpibAddr,
                pdeviceInstance->tmoCount,pdeviceInstance->errorCount,
		pdeviceInstance->queueTimeout,pdeviceInstance->srqWaitTimeout);
            pdeviceInstance = (deviceInstance *)ellNext(&pdeviceInstance->node);
        }
        pdeviceInterface = (deviceInterface *)ellNext(&pdeviceInterface->node);
    }
    pasynQueueManager->freeAsynUser(pasynUser);
}

static void registerSrqHandler(gpibDpvt *pgpibDpvt,
    srqHandler handler,void *userPrivate)
{
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    dbCommon *precord = (dbCommon *)pgpibDpvt->precord;
    gpibDriverUser *pgpibDriverUser = pgpibDpvt->pgpibDriverUser;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    int failure=0;
    
    if(!pgpibDriverUser) {
	printf("%s gpibDriverUser not supported\n",precord->name);
	failure = 1;
    }
    if(pdeviceInstance->unsollicitedHandler) {
	printf("%s an unsollicitedHandler already registered\n",precord->name);
	failure = 1;
    }
    if(failure) {
	precord->pact = TRUE;
    }else {
	pdeviceInstance->unsollicitedHandler = handler;
	pdeviceInstance->userPrivate = userPrivate;
    }
}

#define writeMsgProlog \
    int nchars; \
    dbCommon *precord = (dbCommon *)pgpibDpvt->precord; \
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt); \
    if(!pgpibDpvt->msg) { \
        printf("%s no msg buffer. Must define gpibCmd.msgLen > 0.\n", \
            precord->name); \
        return(1); \
    }

#define writeMsgPostLog \
    if(nchars>pgpibCmd->msgLen) { \
        printf("%s msg buffer too small. msgLen %d message length %d\n", \
            precord->name,pgpibCmd->msgLen,nchars); \
        return(1); \
    } \
    return(0);

static int writeMsgLong(gpibDpvt *pgpibDpvt,long val)
{
    writeMsgProlog
    nchars = epicsSnprintf(pgpibDpvt->msg,pgpibCmd->msgLen,pgpibCmd->format,val);
    writeMsgPostLog
}

static int writeMsgULong(gpibDpvt *pgpibDpvt,unsigned long val)
{
    writeMsgProlog
    nchars = epicsSnprintf(pgpibDpvt->msg,pgpibCmd->msgLen,pgpibCmd->format,val);
    writeMsgPostLog
}

static int writeMsgDouble(gpibDpvt *pgpibDpvt,double val)
{
    writeMsgProlog
    nchars = epicsSnprintf(pgpibDpvt->msg,pgpibCmd->msgLen,pgpibCmd->format,val);
    writeMsgPostLog
}

static int writeMsgString(gpibDpvt *pgpibDpvt,const char *str)
{
    writeMsgProlog
    nchars = epicsSnprintf(pgpibDpvt->msg,pgpibCmd->msgLen,pgpibCmd->format,str);
    writeMsgPostLog
}

static void commonGpibPvtInit(void) 
{
    pcommonGpibPvt = (commonGpibPvt *)callocMustSucceed(1,sizeof(commonGpibPvt),
        "devSupportGpib:commonGpibPvtInit");
    ellInit(&pcommonGpibPvt->deviceInterfaceList);
    pcommonGpibPvt->timerQueue = epicsTimerQueueAllocate(
        1,epicsThreadPriorityScanLow);
}

static deviceInterface *createDeviceInteface(
    int link,asynUser *pasynUser,const char *interfaceName)
{
    deviceInterface *pdeviceInterface;
    deviceDriver *pdeviceDriver;
    asynStatus status;
    int interfaceNameSize;

    interfaceNameSize = strlen(interfaceName) + 1;
    pdeviceInterface = (deviceInterface *)callocMustSucceed(
        1,sizeof(deviceInterface) + interfaceNameSize,"devSupportGpib");
    ellInit(&pdeviceInterface->deviceInstanceList);
    pdeviceInterface->lock = epicsMutexMustCreate();
    pdeviceInterface->link = link;
    pdeviceInterface->interfaceName = (char *)(pdeviceInterface + 1);
    strcpy(pdeviceInterface->interfaceName,interfaceName);
    pdeviceDriver = pasynQueueManager->findDriver(pasynUser,asynDriverType,1);
    if(!pdeviceDriver) {
        printf("devSupportGpib: link %d %s not found\n",link,asynDriverType);
        free(pdeviceInterface);
        return(0);
    }
    pdeviceInterface->pasynDriver =
        (asynDriver *)pdeviceDriver->pdriverInterface->pinterface;
    pdeviceInterface->pasynDriverPvt = pdeviceDriver->pdrvPvt;
    pdeviceDriver = pasynQueueManager->findDriver(pasynUser,octetDriverType,1);
    if(pdeviceDriver) {
        pdeviceInterface->poctetDriver = 
            (octetDriver *)pdeviceDriver->pdriverInterface->pinterface;
        pdeviceInterface->poctetDriverPvt = pdeviceDriver->pdrvPvt;
    }
    pdeviceDriver = pasynQueueManager->findDriver(
        pasynUser,gpibDriverUserType,1);
    if(pdeviceDriver) {
        pdeviceInterface->pgpibDriverUser = 
            (gpibDriverUser *)pdeviceDriver->pdriverInterface->pinterface;
        pdeviceInterface->pgpibDriverUserPvt = pdeviceDriver->pdrvPvt;
        status = pdeviceInterface->pgpibDriverUser->registerSrqHandler(
            pdeviceInterface->pgpibDriverUserPvt,pasynUser,
            srqHandlerGpib,pdeviceInterface);
        if(status!=asynSuccess) {
            printf("%s registerSrqHandler failed %s\n",
                interfaceName,pasynUser->errorMessage);
        }
    }
    ellAdd(&pcommonGpibPvt->deviceInterfaceList,&pdeviceInterface->node);
    return(pdeviceInterface);
}

static int getDeviceInterface(gpibDpvt *pgpibDpvt,int link)
{
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    int gpibAddr = pgpibDpvt->gpibAddr;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    char interfaceName[80];
    deviceInterface *pdeviceInterface;
    deviceInstance *pdeviceInstance;
    asynStatus status;
   
    if(!pcommonGpibPvt) commonGpibPvtInit();
    sprintf(interfaceName,"gpibL%d",link);
    status = pasynQueueManager->connectDevice(pasynUser,interfaceName);
    if(status!=asynSuccess) {
       printf("devSupportGpib:getDeviceInterface link %d %s failed %s\n",
	   link,interfaceName,pasynUser->errorMessage);
       return(1);
    }
    pdeviceInterface = (deviceInterface *)
	ellFirst(&pcommonGpibPvt->deviceInterfaceList);
    while(pdeviceInterface) {
	if(link==pdeviceInterface->link) break;
        pdeviceInterface = (deviceInterface *)ellNext(&pdeviceInterface->node);
    }
    if(!pdeviceInterface) 
        pdeviceInterface = createDeviceInteface(link,pasynUser,interfaceName);
    pdeviceInstance = (deviceInstance *)
        ellFirst(&pdeviceInterface->deviceInstanceList);
    while(pdeviceInstance) {
        if(pdeviceInstance->gpibAddr == gpibAddr) break;
        pdeviceInstance = (deviceInstance *)ellNext(&pdeviceInstance->node);
    }
    if(!pdeviceInstance) {
        pdeviceInstance = (deviceInstance *)callocMustSucceed(
            1,sizeof(deviceInstance),"devSupportGpib");
        ellInit(&pdeviceInstance->waitList);
        pdeviceInstance->gpibAddr = gpibAddr;
        pdeviceInstance->queueTimeout = DEFAULT_QUEUE_TIMEOUT;
        pdeviceInstance->srqWaitTimeout = DEFAULT_SRQ_WAIT_TIMEOUT;
	pdeviceInstance->srqWaitTimer = epicsTimerQueueCreateTimer(
            pcommonGpibPvt->timerQueue,srqWaitTimeout,pdeviceInstance);
        ellAdd(&pdeviceInterface->deviceInstanceList,&pdeviceInstance->node);
    }
    pdevGpibPvt->pdeviceInterface = pdeviceInterface;
    pdevGpibPvt->pdeviceInstance = pdeviceInstance;
    return(0);
}

static void queueIt(gpibDpvt *pgpibDpvt)
{
    dbCommon *precord = pgpibDpvt->precord;
    devGpibParmBlock *pdevGpibParmBlock = pgpibDpvt->pdevGpibParmBlock;
    gpibCmd *pgpibCmd = &pdevGpibParmBlock->gpibCmds[pgpibDpvt->parm];
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    deviceInterface *pdeviceInterface = pdevGpibPvt->pdeviceInterface;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    asynStatus status;

    epicsMutexMustLock(pdeviceInterface->lock);
    if(pdeviceInstance->waitForSRQ && !pdeviceInstance->queueRequestFromSrq) {
	ellAdd(&pdeviceInstance->waitList,&pgpibDpvt->node);
	epicsMutexUnlock(pdeviceInterface->lock);
        precord->pact = TRUE;
        return;
    }
    status = pasynQueueManager->queueRequest(pgpibDpvt->pasynUser,
        pgpibCmd->pri,pdeviceInstance->queueTimeout);
    if(status!=asynSuccess) {
        epicsMutexUnlock(pdeviceInterface->lock);
        printf("%s queueRequest failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
        recGblSetSevr(precord, SOFT_ALARM, INVALID_ALARM);
        return;
    }
    epicsMutexUnlock(pdeviceInterface->lock);
    precord->pact = TRUE;
}

static void gpibRead(gpibDpvt *pgpibDpvt,int timeoutOccured)
{
    dbCommon *precord = pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    deviceInterface *pdeviceInterface = pdevGpibPvt->pdeviceInterface;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    asynStatus status;
    octetDriver *poctetDriver = pgpibDpvt->poctetDriver;
    void *poctetDriverPvt = pgpibDpvt->poctetDriverPvt;
    int nchars,lenmsg;
    int cmdType = gpibCmdTypeNoEOS(pgpibCmd->type);

    if(pgpibDpvt->pdevGpibParmBlock->debugFlag)
        printf("%s gpibRead\n",precord->name);
    if(timeoutOccured) {
        pdevGpibPvt->finish(pgpibDpvt,1);
        return;
    }
    if(!pgpibDpvt->msg) {
        printf("%s no msg buffer\n",precord->name);
        recGblSetSevr(precord,READ_ALARM, INVALID_ALARM);
        return;
    }
    epicsMutexMustLock(pdeviceInterface->lock);
    if(pdeviceInstance->waitForSRQ) {
	ellAdd(&pdeviceInstance->waitList,&pgpibDpvt->node);
	epicsMutexUnlock(pdeviceInterface->lock);
	return;
    }
    if(cmdType&(GPIBEFASTI|GPIBEFASTIW)) {
        pdeviceInstance->waitForSRQ = 1;
        pdeviceInstance->pgpibDpvt = pgpibDpvt;
        pdevGpibPvt->work = gpibReadWaitComplete;
        epicsTimerStartDelay(pdeviceInstance->srqWaitTimer,
            pdeviceInstance->srqWaitTimeout);
    }
    epicsMutexUnlock(pdeviceInterface->lock);
    switch(cmdType) {
    case GPIBREADW:
    case GPIBEFASTIW:
    case GPIBREAD:
    case GPIBEFASTI:
	lenmsg = strlen(pgpibCmd->cmd);
        nchars = writeIt(pgpibDpvt,pgpibCmd->cmd,lenmsg);
        if(nchars!=lenmsg) {
            if(cmdType&(GPIBREADW|GPIBEFASTIW)) {
                epicsTimerCancel(pdeviceInstance->srqWaitTimer);
                srqWaitTimeout(pdeviceInstance);
	        break;
            }
            pdevGpibPvt->finish(pgpibDpvt,1);
            break;
        }
        if(cmdType&(GPIBREADW|GPIBEFASTIW)) break;
    case GPIBRAWREAD:
        nchars = poctetDriver->read(poctetDriverPvt,pgpibDpvt->pasynUser,
            pgpibDpvt->gpibAddr,pgpibDpvt->msg,pgpibCmd->msgLen);
        if(pgpibDpvt->pdevGpibParmBlock->debugFlag)
            printf("%s msg %s nchars %d\n",precord->name,pgpibDpvt->msg,nchars);
        if(nchars==0) {
	    ++pdeviceInstance->tmoCount;
            pdevGpibPvt->finish(pgpibDpvt,1);
	    break;
	}
        if(nchars<pgpibCmd->msgLen) pgpibDpvt->msg[nchars] = 0;
        if(cmdType&(GPIBEFASTI|GPIBEFASTIW)) 
            pgpibDpvt->efastVal = checkEnums(pgpibDpvt->msg, pgpibCmd->P3);
        pdevGpibPvt->finish(pgpibDpvt,0);
        break;
    default:
        printf("%s gpibRead can't handle cmdType %d"
               " record left with PACT true\n",precord->name,cmdType);
    }
    status = pasynQueueManager->unlock(pgpibDpvt->pasynUser);
    if(status!=asynSuccess) {
        printf("%s pasynQueueManager->unlock failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
    }
}

static void gpibReadWaitComplete(gpibDpvt *pgpibDpvt,int timeoutOccured)
{
    dbCommon *precord = pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    deviceInterface *pdeviceInterface = pdevGpibPvt->pdeviceInterface;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    octetDriver *poctetDriver = pgpibDpvt->poctetDriver;
    void *poctetDriverPvt = pgpibDpvt->poctetDriverPvt;
    int nchars;
    int cmdType = gpibCmdTypeNoEOS(pgpibCmd->type);
    int failure;

    if(pgpibDpvt->pdevGpibParmBlock->debugFlag)
        printf("%s gpibReadWaitComplete\n",precord->name);
    if(timeoutOccured) {
        pdevGpibPvt->finish(pgpibDpvt,1);
        return;
    }
    failure = waitForSRQClear(pgpibDpvt,pdeviceInterface,pdeviceInstance);
    if(failure) return;
    nchars = poctetDriver->read(poctetDriverPvt,pgpibDpvt->pasynUser,
        pgpibDpvt->gpibAddr,pgpibDpvt->msg,pgpibCmd->msgLen);
    if(nchars==0) {
        ++pdeviceInstance->tmoCount;
        pdevGpibPvt->finish(pgpibDpvt,1);
        return;
    }
    if(pgpibDpvt->pdevGpibParmBlock->debugFlag)
        printf("%s msg %s nchars %d\n",precord->name,pgpibDpvt->msg,nchars);
    if(nchars<pgpibCmd->msgLen) pgpibDpvt->msg[nchars] = 0;
    if(cmdType&(GPIBEFASTI|GPIBEFASTIW)) 
        pgpibDpvt->efastVal = checkEnums(pgpibDpvt->msg, pgpibCmd->P3);
    pdevGpibPvt->finish(pgpibDpvt,0);
}

static void gpibWrite(gpibDpvt *pgpibDpvt,int timeoutOccured)
{
    dbCommon *precord = pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    deviceInterface *pdeviceInterface = pdevGpibPvt->pdeviceInterface;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    gpibDriverUser *pgpibDriverUser = pgpibDpvt->pgpibDriverUser;
    void *pgpibDriverUserPvt = pgpibDpvt->pgpibDriverUserPvt;
    int cmdType = gpibCmdTypeNoEOS(pgpibCmd->type);
    int nchars = 0;
    int lenMessage = 0;
    int failure = 0;
    char *efasto1, *efasto2;

    if(pgpibDpvt->pdevGpibParmBlock->debugFlag)
        printf("%s gpibWrite\n",precord->name);
    if(timeoutOccured) {
        pdevGpibPvt->finish(pgpibDpvt,1);
        return;
    }
    epicsMutexMustLock(pdeviceInterface->lock);
    if(pdeviceInstance->waitForSRQ) {
	ellAdd(&pdeviceInstance->waitList,&pgpibDpvt->node);
	return;
    }
    epicsMutexUnlock(pdeviceInterface->lock);
    switch(cmdType) {
    case GPIBWRITE:
    case GPIBCMD:
        lenMessage = strlen(pgpibDpvt->msg);
	nchars = writeIt(pgpibDpvt,pgpibDpvt->msg,lenMessage);
	break;
    case GPIBACMD:
        if(!pgpibDriverUser) {
            printf("%s gpibWrite got GPIBRAWREAD but pgpibDriverUser = 0"
               " record left with PACT true\n",precord->name);
            break;
        }
        lenMessage = strlen(pgpibDpvt->msg);
        nchars = pgpibDriverUser->addressedCmd(
            pgpibDriverUserPvt,pgpibDpvt->pasynUser,
            pgpibDpvt->gpibAddr,pgpibDpvt->msg,strlen(pgpibDpvt->msg));
        break;
    case GPIBEFASTO:    /* write the enumerated cmd from the P3 array */
        /* bfr added: cmd is not ignored but evaluated as prefix to
         * pgpibCmd->P3[pgpibDpvt->efastVal] (cmd is _not_ intended to be an
         * independent command in itself). */
	efasto1 = pgpibCmd->cmd;
	if(!efasto1) efasto1 = "";
	efasto2 = pgpibCmd->P3[pgpibDpvt->efastVal];
	if(!efasto2) efasto2 = "";
        if(pgpibDpvt->msg
        && (pgpibCmd->msgLen > (strlen(efasto1)+strlen(efasto2)))) {
            sprintf(pgpibDpvt->msg, "%s%s", efasto1, efasto2);
            lenMessage = strlen(pgpibDpvt->msg);
	    nchars = writeIt(pgpibDpvt,pgpibDpvt->msg,lenMessage);
        } else {
            recGblSetSevr(precord,READ_ALARM, INVALID_ALARM);
            printf("%s() no msg buffer or msgLen too small\n",precord->name);
        }
        break;
    default:
        printf("%s gpibWrite cant handle cmdType %d"
               " record left with PACT true\n",precord->name,cmdType);
        return;
    }
    if(nchars!=lenMessage) failure = 1;
    pdevGpibPvt->finish(pgpibDpvt,failure);
}

static void queueCallback(void *puserPvt)
{
    gpibDpvt *pgpibDpvt = (gpibDpvt *)puserPvt;
    dbCommon *precord = pgpibDpvt->precord;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    int failure = 0;

    if(pgpibDpvt->pdevGpibParmBlock->debugFlag)
        printf("%s queueCallback\n",precord->name);
    if(!precord->pact) {
	printf("%s devSupportGpib:queueCallback but pact 0. Request ignored.\n",
            precord->name);
	return;
    }
    if(pdeviceInstance->timeoutActive){
	epicsTimeStamp timeNow;
	double diff;

	epicsTimeGetCurrent(&timeNow);
	diff = epicsTimeDiffInSeconds(&timeNow,&pdeviceInstance->timeoutTime);
	if(diff < pgpibDpvt->pdevGpibParmBlock->timeWindow) {
            failure = 1;
	}else {
	    pdeviceInstance->timeoutActive = 0;
	}
    }
    assert(pdevGpibPvt->work);
    (pdevGpibPvt->work)(pgpibDpvt,failure);
}

static void timeoutCallback(void *puserPvt)
{
    gpibDpvt *pgpibDpvt = (gpibDpvt *)puserPvt;
    dbCommon *precord = pgpibDpvt->precord;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;

    if(pgpibDpvt->pdevGpibParmBlock->debugFlag)
        printf("%s timeoutCallback\n",precord->name);
    if(!precord->pact) {
	printf("%s devSupportGpib:timeoutCallback but pact 0. Request ignored.\n",
            precord->name);
	return;
    }
    assert(pdevGpibPvt->work);
    (pdevGpibPvt->work)(pgpibDpvt,1);
}

void srqHandlerGpib(void *parm, int gpibAddr, int statusByte)
{
    deviceInterface *pdeviceInterface = (deviceInterface *)parm;
    deviceInstance *pdeviceInstance;

    epicsMutexMustLock(pdeviceInterface->lock);
    pdeviceInstance = (deviceInstance *)ellFirst(
        &pdeviceInterface->deviceInstanceList);
    while(pdeviceInstance) {
	if(pdeviceInstance->gpibAddr!=gpibAddr) {
	    pdeviceInstance = (deviceInstance *)ellNext(&pdeviceInstance->node);
	    continue;
	}
        if(pdeviceInstance->waitForSRQ) {
            epicsTimerCancel(pdeviceInstance->srqWaitTimer);
	    pdeviceInstance->queueRequestFromSrq = 1;
            queueIt(pdeviceInstance->pgpibDpvt);
	    /*Must unlock after queueIt to prevent possible race condition*/
	    pdeviceInstance->queueRequestFromSrq = 0;
            epicsMutexUnlock(pdeviceInterface->lock);
            return;
        } else if(pdeviceInstance->unsollicitedHandler) {
            epicsMutexUnlock(pdeviceInterface->lock);
	    pdeviceInstance->unsollicitedHandler(
                pdeviceInstance->userPrivate,gpibAddr,statusByte);
            return;
	}
	break;
    }
    epicsMutexUnlock(pdeviceInterface->lock);
    printf("interfaceName %s link %d gpibAddr %d "
           "SRQ happened but no record is attached to the gpibAddr\n",
            pdeviceInterface->interfaceName,pdeviceInterface->link,gpibAddr);
}

void srqWaitTimeout(void *parm)
{
    deviceInstance *pdeviceInstance = (deviceInstance *)parm;
    gpibDpvt *pgpibDpvt = pdeviceInstance->pgpibDpvt;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    deviceInterface *pdeviceInterface = pdevGpibPvt->pdeviceInterface;
    int failure;

    failure = waitForSRQClear(pgpibDpvt,pdeviceInterface,pdeviceInstance);
    if(!failure) gpibReadWaitComplete(pgpibDpvt,1);
}

static int checkEnums(char *msg, char **enums)
{
    int i = 0;

    while (enums[i] != 0) {
        int j = 0;
	while(enums[i][j] && (enums[i][j] == msg[j]) ) j++;
	if (enums[i][j] == 0) return (i);
	i++;
    }
    return 0;
}

static int waitForSRQClear(gpibDpvt *pgpibDpvt,
    deviceInterface *pdeviceInterface,deviceInstance *pdeviceInstance)
{
    gpibDpvt *pgpibDpvtWait;

    epicsMutexMustLock(pdeviceInterface->lock);
    /*check that pgpibDpvt is owner of waitForSRQ*/
    if(pdeviceInstance->pgpibDpvt!=pgpibDpvt) {
        epicsMutexUnlock(pdeviceInterface->lock);
	return(1); /*return failure*/
    }
    pdeviceInstance->waitForSRQ = 0;
    pdeviceInstance->pgpibDpvt = 0;
    pdeviceInstance->pgpibDpvt = 0;
    pdeviceInstance->queueRequestFromSrq = 0;
    pgpibDpvtWait = (gpibDpvt *)ellFirst(&pdeviceInstance->waitList);
    while(pgpibDpvtWait) {
        gpibDpvt *pnext;
         
        pnext = (gpibDpvt *)ellNext(&pgpibDpvtWait->node);
        ellDelete(&pdeviceInstance->waitList,&pgpibDpvtWait->node);
        /*Note that this requires recursive lock*/
	queueIt(pgpibDpvtWait);
        pgpibDpvtWait = pnext;
    }
    epicsMutexUnlock(pdeviceInterface->lock);
    return(0); /*return success*/
}

static int writeIt(gpibDpvt *pgpibDpvt,char *message,int len)
{
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    char *rsp = pgpibDpvt->rsp;
    int rspLen = pgpibCmd->rspLen;
    dbCommon *precord = pgpibDpvt->precord;
    octetDriver *poctetDriver = pgpibDpvt->poctetDriver;
    void *poctetDriverPvt = pgpibDpvt->poctetDriverPvt;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    int respond2Writes = pgpibDpvt->pdevGpibParmBlock->respond2Writes;
    int nchars;

    if(pgpibDpvt->pdevGpibParmBlock->debugFlag)
        printf("%s writeIt message %s len %d\n",precord->name,message,len);
    nchars = poctetDriver->write(poctetDriverPvt,pgpibDpvt->pasynUser,
        pgpibDpvt->gpibAddr,message,len);
    if(nchars!=len) {
        if(nchars==0) {
            ++pdeviceInstance->tmoCount;
        } else {
            ++pdeviceInstance->errorCount;
        }
    }
    if(respond2Writes>=0 && rspLen>0) {
        int nread;

        if(rspLen<len) {
            printf("%s respond2Writes but rspLen %d < len %d\n",
                precord->name,rspLen,len);
        } else {
            if(pgpibDpvt->pdevGpibParmBlock->debugFlag)
                printf("%s respond2Writes\n",precord->name);
            if(respond2Writes>0) epicsThreadSleep((double)(respond2Writes*1000));
            nread = poctetDriver->read(poctetDriverPvt,pgpibDpvt->pasynUser,
                pgpibDpvt->gpibAddr,rsp,len);
            if(nread!=rspLen) {
                printf("%s respond2Writes but nread %d for %d length message\n",
                    precord->name,nread,len);
            }
        }
    }
    return(nchars);
}

#define devGpibDeviceInterfaceSetCommon \
    deviceInterface  *pdeviceInterface;\
    deviceInstance *pdeviceInstance;\
\
    if(!pcommonGpibPvt) commonGpibPvtInit();\
    pdeviceInterface = (deviceInterface *)ellFirst(\
        &pcommonGpibPvt->deviceInterfaceList);\
    while(pdeviceInterface) {\
        if(strcmp(interfaceName,pdeviceInterface->interfaceName)==0) break;\
	pdeviceInterface = (deviceInterface *)ellNext(&pdeviceInterface->node);\
    }\
    if(!pdeviceInterface) {\
	printf("%s no found\n",interfaceName);\
	return;\
    }\
    pdeviceInstance = (deviceInstance *)ellFirst(\
         &pdeviceInterface->deviceInstanceList);\
    while(pdeviceInstance) {\
	if(gpibAddr==pdeviceInstance->gpibAddr) break;\
	pdeviceInstance = (deviceInstance *)ellNext(&pdeviceInstance->node);\
    }\
    if(!pdeviceInstance) {\
	printf("gpibAddr %d not found\n",gpibAddr);\
	return;\
    }

static void devGpibQueueTimeoutSet(
    const char *interfaceName, int gpibAddr, double timeout)
{
    devGpibDeviceInterfaceSetCommon

    pdeviceInstance->queueTimeout = timeout;
}

static void devGpibSrqWaitTimeoutSet(
    const char *interfaceName, int gpibAddr, double timeout)
{
    devGpibDeviceInterfaceSetCommon

    pdeviceInstance->srqWaitTimeout = timeout;
}

static const iocshArg devGpibReportArg0 = {"level", iocshArgInt};
static const iocshArg *const devGpibReportArgs[1] = {&devGpibReportArg0};
static const iocshFuncDef devGpibReportDef =
    {"devGpibReport", 1, devGpibReportArgs};
static void devGpibReportCall(const iocshArgBuf * args) {
        pdevSupportGpib->report(args[0].ival);
}

static const iocshArg devGpibQueueTimeoutArg0 =
 {"interfaceName",iocshArgString};
static const iocshArg devGpibQueueTimeoutArg1 = {"gpibAddr",iocshArgInt};
static const iocshArg devGpibQueueTimeoutArg2 = {"timeout",iocshArgDouble};
static const iocshArg *const devGpibQueueTimeoutArgs[3] =
 {&devGpibQueueTimeoutArg0,&devGpibQueueTimeoutArg1,&devGpibQueueTimeoutArg2};
static const iocshFuncDef devGpibQueueTimeoutDef =
    {"devGpibQueueTimeout", 3, devGpibQueueTimeoutArgs};
static void devGpibQueueTimeoutCall(const iocshArgBuf * args) {
    devGpibQueueTimeoutSet(args[0].sval,args[1].ival,args[2].dval);
}

static const iocshArg devGpibSrqWaitTimeoutArg0 =
 {"interfaceName",iocshArgString};
static const iocshArg devGpibSrqWaitTimeoutArg1 = {"gpibAddr",iocshArgInt};
static const iocshArg devGpibSrqWaitTimeoutArg2 = {"timeout",iocshArgDouble};
static const iocshArg *const devGpibSrqWaitTimeoutArgs[3] =
 {&devGpibSrqWaitTimeoutArg0,&devGpibSrqWaitTimeoutArg1,&devGpibSrqWaitTimeoutArg2};
static const iocshFuncDef devGpibSrqWaitTimeoutDef =
    {"devGpibSrqWaitTimeout", 3, devGpibSrqWaitTimeoutArgs};
static void devGpibSrqWaitTimeoutCall(const iocshArgBuf * args) {
    devGpibSrqWaitTimeoutSet(args[0].sval,args[1].ival,args[2].dval);
}

static void devGpib(void)
{
    static int firstTime = 1;
    if(!firstTime) return;
    firstTime = 0;
    iocshRegister(&devGpibReportDef,devGpibReportCall);
    iocshRegister(&devGpibQueueTimeoutDef,devGpibQueueTimeoutCall);
    iocshRegister(&devGpibSrqWaitTimeoutDef,devGpibSrqWaitTimeoutCall);
}
epicsExportRegistrar(devGpib);
