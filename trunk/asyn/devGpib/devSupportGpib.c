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
#include <asynGpibDriver.h>

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
    void *unsollicitedHandlerPvt;
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
    asynCommon *pasynCommon;
    void *asynCommonPvt;
    asynOctet *pasynOctet;
    void *asynOctetPvt;
    asynGpib *pasynGpib;
    void *asynGpibPvt;
    void *pupvt;                /* user defined pointer */
}deviceInterface;

struct devGpibPvt {
    deviceInterface *pdeviceInterface;
    deviceInstance *pdeviceInstance;
    gpibWork work;
    gpibWork finish;
    char eos[2]; /*If GPIBEOS is specified*/
};

static long initRecord(dbCommon* precord, struct link * plink);
static long processGPIBSOFT(gpibDpvt *pgpibDpvt);
static void queueReadRequest(gpibDpvt *pgpibDpvt, gpibWork finish);
static void queueWriteRequest(gpibDpvt *pgpibDpvt, gpibWork finish);
static void queueRequest(gpibDpvt *pgpibDpvt, gpibWork work);
static void report(int level);
static void registerSrqHandler(gpibDpvt *pgpibDpvt,
    srqHandler handler,void *unsollicitedHandlerPvt);
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
static int getDeviceInterface(gpibDpvt *pgpibDpvt,int link,int gpibAddr);

/*Process routines */
static void queueIt(gpibDpvt *pgpibDpvt,int isLocked);
static void gpibRead(gpibDpvt *pgpibDpvt,int timeoutOccured);
static void gpibReadWaitComplete(gpibDpvt *pgpibDpvt,int timeoutOccured);
static void gpibWrite(gpibDpvt *pgpibDpvt,int timeoutOccured);

/*Callback routines*/
static void queueCallback(asynUser *pasynUser);
static void timeoutCallback(asynUser *pasynUser);
void srqHandlerGpib(void *parm, int gpibAddr, int statusByte);
void srqWaitTimeout(void *parm);

/*Utility routines*/
static int checkEnums(char * msg, char **enums);
static int waitForSRQClear(gpibDpvt *pgpibDpvt,
    deviceInterface *pdeviceInterface,deviceInstance *pdeviceInstance);
static int isTimeWindowActive(gpibDpvt *pgpibDpvt);
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
    asynCommon *pasynCommon;
    asynOctet *pasynOctet;
    

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
    pasynUser = pasynManager->createAsynUser(
        queueCallback,timeoutCallback);
    pasynUser->userPvt = pgpibDpvt;
    pasynUser->timeout = pdevGpibParmBlock->timeout;
    pgpibDpvt->pasynUser = pasynUser;
    pgpibDpvt->pdevGpibParmBlock = pdevGpibParmBlock;
    pgpibDpvt->precord = precord;
    pgpibDpvt->parm = parm;
    if(getDeviceInterface(pgpibDpvt,link,gpibAddr)) {
        printf("%s: init_record : no driver for link %d\n",precord->name,link);
        precord->pact = TRUE;	/* keep record from being processed */
	free(pasynUser);
        return(0);
    }
    pgpibCmd = gpibCmdGet(pgpibDpvt);
    pdeviceInterface = pdevGpibPvt->pdeviceInterface;
    pasynCommon = pdeviceInterface->pasynCommon;
    pasynOctet = pdeviceInterface->pasynOctet;
    if(!pasynCommon || !pasynOctet) {
        printf("%s: init_record : pasynCommon %p pasynOctet %p\n",
           precord->name,pgpibDpvt->pasynCommon,pgpibDpvt->pasynOctet);
        precord->pact = TRUE;	/* keep record from being processed */
	free(pasynUser);
        return(0);
    }
    pgpibDpvt->pasynCommon = pasynCommon;
    pgpibDpvt->asynCommonPvt = pdeviceInterface->asynCommonPvt;
    pgpibDpvt->pasynOctet = pasynOctet;
    pgpibDpvt->asynOctetPvt = pdeviceInterface->asynOctetPvt;
    if(pgpibCmd->type&GPIBEOS) {
        pdevGpibPvt->eos[0] = (char)pgpibCmd->eosChar;
        status = pgpibDpvt->pasynOctet->setEos(
            pgpibDpvt->asynOctetPvt,pgpibDpvt->pasynUser,
            pdevGpibPvt->eos,1);
        if(status!=asynSuccess) {
            printf("%s pasynOctet->setEos failed %s\n",
                precord->name,pgpibDpvt->pasynUser->errorMessage);
        }
    }
    pgpibDpvt->pasynGpib = pdeviceInterface->pasynGpib;
    pgpibDpvt->asynGpibPvt = pdeviceInterface->asynGpibPvt;
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

    if(*pgpibDpvt->pdevGpibParmBlock->debugFlag>=2)
        printf("%s queueReadRequest\n",precord->name);
    if(!pgpibDpvt->msg) {
        printf("%s no msg buffer\n",precord->name);
        recGblSetSevr(precord,READ_ALARM, INVALID_ALARM);
        return;
    }
    pdevGpibPvt->work = gpibRead;
    pdevGpibPvt->finish = finish;
    status = pasynManager->lock(pgpibDpvt->pasynUser);
    if(status!=asynSuccess) {
        printf("%s pasynManager->lock failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
        recGblSetSevr(precord, SOFT_ALARM, INVALID_ALARM);
        return;
    }
    queueIt(pgpibDpvt,0);
}

static void queueWriteRequest(gpibDpvt *pgpibDpvt, gpibWork finish)
{
    dbCommon *precord = pgpibDpvt->precord;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;

    if(*pgpibDpvt->pdevGpibParmBlock->debugFlag>=2)
        printf("%s queueWriteRequest\n",precord->name);
    pdevGpibPvt->work = gpibWrite;
    pdevGpibPvt->finish = finish;
    queueIt(pgpibDpvt,0);
}
static void queueRequest(gpibDpvt *pgpibDpvt, gpibWork work)
{
    dbCommon *precord = pgpibDpvt->precord;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;

    if(*pgpibDpvt->pdevGpibParmBlock->debugFlag>=2)
        printf("%s queueRequest\n",precord->name);
    pdevGpibPvt->work = work;
    pdevGpibPvt->finish = 0;
    queueIt(pgpibDpvt,0);
}

static void report(int interest)
{
    asynUser *pasynUser;
    deviceInterface  *pdeviceInterface;
    deviceInstance *pdeviceInstance;

    if(!pcommonGpibPvt) commonGpibPvtInit();
    pasynUser = pasynManager->createAsynUser(0,0);
    pdeviceInterface = (deviceInterface *)ellFirst(
        &pcommonGpibPvt->deviceInterfaceList);
    while(pdeviceInterface) {
        printf("link %d interfaceName %s\n",
            pdeviceInterface->link,pdeviceInterface->interfaceName);
        printf("    pasynCommon %p pasynOctet %p pasynGpib %p\n",
            pdeviceInterface->pasynCommon,pdeviceInterface->pasynOctet,
            pdeviceInterface->pasynGpib);
        if(pdeviceInterface->pasynCommon) {
            pdeviceInterface->pasynCommon->report(
                pdeviceInterface->asynCommonPvt,stdout,interest);
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
    pasynManager->freeAsynUser(pasynUser);
}

static void registerSrqHandler(gpibDpvt *pgpibDpvt,
    srqHandler handler,void *unsollicitedHandlerPvt)
{
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    dbCommon *precord = (dbCommon *)pgpibDpvt->precord;
    asynGpib *pasynGpib = pgpibDpvt->pasynGpib;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    deviceInterface *pdeviceInterface = pdevGpibPvt->pdeviceInterface;
    int failure=0;
    
    if(!pasynGpib) {
	printf("%s asynGpib not supported\n",precord->name);
	failure = 1;
    }
    if(pdeviceInstance->unsollicitedHandler) {
	printf("%s an unsollicitedHandler already registered\n",precord->name);
	failure = 1;
    } else if (!pdeviceInterface->pasynGpib) {
        printf("%s asynGpib not supported\n",precord->name);
        failure = 1;
    }
    if(failure) {
	precord->pact = TRUE;
    }else {
	pdeviceInstance->unsollicitedHandlerPvt = unsollicitedHandlerPvt;
	pdeviceInstance->unsollicitedHandler = handler;
        if(!pdeviceInstance->waitForSRQ) {
            pdeviceInterface->pasynGpib->pollAddr(
                pdeviceInterface->asynGpibPvt,pgpibDpvt->pasynUser,1);
        }
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
    asynInterface *pasynInterface;
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
    pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,1);
    if(!pasynInterface) {
        printf("devSupportGpib: link %d %s not found\n",link,asynCommonType);
        free(pdeviceInterface);
        return(0);
    }
    pdeviceInterface->pasynCommon =
        (asynCommon *)pasynInterface->pinterface;
    pdeviceInterface->asynCommonPvt = pasynInterface->drvPvt;
    pasynInterface = pasynManager->findInterface(pasynUser,asynOctetType,1);
    if(pasynInterface) {
        pdeviceInterface->pasynOctet = 
            (asynOctet *)pasynInterface->pinterface;
        pdeviceInterface->asynOctetPvt = pasynInterface->drvPvt;
    }
    pasynInterface = pasynManager->findInterface(
        pasynUser,asynGpibType,1);
    if(pasynInterface) {
        pdeviceInterface->pasynGpib = 
            (asynGpib *)pasynInterface->pinterface;
        pdeviceInterface->asynGpibPvt = pasynInterface->drvPvt;
        status = pdeviceInterface->pasynGpib->registerSrqHandler(
            pdeviceInterface->asynGpibPvt,pasynUser,
            srqHandlerGpib,pdeviceInterface);
        if(status!=asynSuccess) {
            printf("%s registerSrqHandler failed %s\n",
                interfaceName,pasynUser->errorMessage);
        }
    }
    ellAdd(&pcommonGpibPvt->deviceInterfaceList,&pdeviceInterface->node);
    return(pdeviceInterface);
}

static int getDeviceInterface(gpibDpvt *pgpibDpvt,int link,int gpibAddr)
{
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    char interfaceName[80];
    deviceInterface *pdeviceInterface;
    deviceInstance *pdeviceInstance;
    asynStatus status;
   
    if(!pcommonGpibPvt) commonGpibPvtInit();
    sprintf(interfaceName,"gpibL%d",link);
    status = pasynManager->connectDevice(pasynUser,interfaceName,gpibAddr);
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
    if(!pdeviceInterface) {
        pdeviceInterface = createDeviceInteface(link,pasynUser,interfaceName);
        if(!pdeviceInterface) return(1);
    }
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

static void queueIt(gpibDpvt *pgpibDpvt,int isLocked)
{
    dbCommon *precord = pgpibDpvt->precord;
    devGpibParmBlock *pdevGpibParmBlock = pgpibDpvt->pdevGpibParmBlock;
    gpibCmd *pgpibCmd = &pdevGpibParmBlock->gpibCmds[pgpibDpvt->parm];
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    deviceInterface *pdeviceInterface = pdevGpibPvt->pdeviceInterface;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    asynStatus status;

    if(!isLocked)epicsMutexMustLock(pdeviceInterface->lock);
    if(pdeviceInstance->timeoutActive) {
        if(isTimeWindowActive(pgpibDpvt)) {
            precord->pact = FALSE;
            recGblSetSevr(precord, SOFT_ALARM, INVALID_ALARM);
            if(!isLocked)epicsMutexUnlock(pdeviceInterface->lock);
            printf("%s queueRequest failed timeWindow active\n",
                precord->name);
            return;
        }
    }
    if(pdeviceInstance->waitForSRQ && !pdeviceInstance->queueRequestFromSrq) {
        precord->pact = TRUE;
	ellAdd(&pdeviceInstance->waitList,&pgpibDpvt->node);
	if(!isLocked)epicsMutexUnlock(pdeviceInterface->lock);
        return;
    }
    precord->pact = TRUE;
    status = pasynManager->queueRequest(pgpibDpvt->pasynUser,
        pgpibCmd->pri,pdeviceInstance->queueTimeout);
    if(status!=asynSuccess) {
        precord->pact = FALSE;
        recGblSetSevr(precord, SOFT_ALARM, INVALID_ALARM);
        if(!isLocked)epicsMutexUnlock(pdeviceInterface->lock);
        printf("%s queueRequest failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
        return;
    }
    if(!isLocked)epicsMutexUnlock(pdeviceInterface->lock);
}

static void gpibRead(gpibDpvt *pgpibDpvt,int timeoutOccured)
{
    dbCommon *precord = pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdTypeNoEOS(pgpibCmd->type);
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    deviceInterface *pdeviceInterface = pdevGpibPvt->pdeviceInterface;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    asynOctet *pasynOctet = pgpibDpvt->pasynOctet;
    void *asynOctetPvt = pgpibDpvt->asynOctetPvt;
    int nchars = 0,lenmsg = 0;
    asynStatus status;

    if(*pgpibDpvt->pdevGpibParmBlock->debugFlag>=2)
        printf("%s gpibRead\n",precord->name);
    if(timeoutOccured) {
        pdevGpibPvt->finish(pgpibDpvt,1);
        goto done;
    }
    epicsMutexMustLock(pdeviceInterface->lock);
    /*Since queueReadRequest calls lock waitForSRQ should not be true*/
    assert(!pdeviceInstance->waitForSRQ);
    if(cmdType&(GPIBREADW|GPIBEFASTIW)) {
        pdeviceInstance->waitForSRQ = 1;
        pdeviceInstance->pgpibDpvt = pgpibDpvt;
        if(!pdeviceInstance->unsollicitedHandler) {
            pdeviceInterface->pasynGpib->pollAddr(
                pdeviceInterface->asynGpibPvt,pgpibDpvt->pasynUser,1);
        }
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
        if(!pgpibCmd->cmd) {
            printf("%s pgpibCmd->cmd is null\n",precord->name);
            recGblSetSevr(precord,READ_ALARM, INVALID_ALARM);
        } else {
	    lenmsg = strlen(pgpibCmd->cmd);
            nchars = writeIt(pgpibDpvt,pgpibCmd->cmd,lenmsg);
        }
        if(!pgpibCmd->cmd || nchars!=lenmsg) {
            if(cmdType&(GPIBREADW|GPIBEFASTIW)) {
                epicsTimerCancel(pdeviceInstance->srqWaitTimer);
                srqWaitTimeout(pdeviceInstance);
	        break;
            }
            pdevGpibPvt->finish(pgpibDpvt,1);
            break;
        }
        if(cmdType&(GPIBREADW|GPIBEFASTIW)) goto done;
    case GPIBRAWREAD:
        if(!pgpibDpvt->msg) {
            printf("%s pgpibDpvt->msg is null\n",precord->name);
            recGblSetSevr(precord,READ_ALARM, INVALID_ALARM);
            nchars = 0;
        } else {
            nchars = pasynOctet->read(asynOctetPvt,pgpibDpvt->pasynUser,
                pgpibDpvt->msg,pgpibCmd->msgLen);
        }
        if(*pgpibDpvt->pdevGpibParmBlock->debugFlag>=2)
            printf("%s msg %s nchars %d\n",precord->name,pgpibDpvt->msg,nchars);
        if(nchars==0) {
	    ++pdeviceInstance->tmoCount;
            pdevGpibPvt->finish(pgpibDpvt,1);
	    break;
	}
        if(nchars<pgpibCmd->msgLen) pgpibDpvt->msg[nchars] = 0;
        if(cmdType&GPIBEFASTI) 
            pgpibDpvt->efastVal = checkEnums(pgpibDpvt->msg, pgpibCmd->P3);
        pdevGpibPvt->finish(pgpibDpvt,0);
        break;
    default:
        printf("%s gpibRead can't handle cmdType %d"
               " record left with PACT true\n",precord->name,cmdType);
    }
done:
    status = pasynManager->unlock(pgpibDpvt->pasynUser);
    if(status!=asynSuccess) {
        printf("%s pasynManager->unlock failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
    }
}

static void gpibReadWaitComplete(gpibDpvt *pgpibDpvt,int timeoutOccured)
{
    dbCommon *precord = pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdTypeNoEOS(pgpibCmd->type);
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    deviceInterface *pdeviceInterface = pdevGpibPvt->pdeviceInterface;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    asynOctet *pasynOctet = pgpibDpvt->pasynOctet;
    void *asynOctetPvt = pgpibDpvt->asynOctetPvt;
    int nchars,failure;

    if(*pgpibDpvt->pdevGpibParmBlock->debugFlag>=2)
        printf("%s gpibReadWaitComplete\n",precord->name);
    if(timeoutOccured) {
        pdevGpibPvt->finish(pgpibDpvt,1);
        return;
    }
    if(!pdeviceInstance->unsollicitedHandler) {
        pdeviceInterface->pasynGpib->pollAddr(
            pdeviceInterface->asynGpibPvt,pgpibDpvt->pasynUser,0);
    }
    failure = waitForSRQClear(pgpibDpvt,pdeviceInterface,pdeviceInstance);
    if(failure) return;
    nchars = pasynOctet->read(asynOctetPvt,pgpibDpvt->pasynUser,
        pgpibDpvt->msg,pgpibCmd->msgLen);
    if(nchars==0) {
        ++pdeviceInstance->tmoCount;
        pdevGpibPvt->finish(pgpibDpvt,1);
        return;
    }
    if(*pgpibDpvt->pdevGpibParmBlock->debugFlag>=2)
        printf("%s msg %s nchars %d\n",precord->name,pgpibDpvt->msg,nchars);
    if(nchars<pgpibCmd->msgLen) pgpibDpvt->msg[nchars] = 0;
    if(cmdType&GPIBEFASTIW) 
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
    asynGpib *pasynGpib = pgpibDpvt->pasynGpib;
    void *asynGpibPvt = pgpibDpvt->asynGpibPvt;
    int cmdType = gpibCmdTypeNoEOS(pgpibCmd->type);
    int nchars = 0, lenMessage = 0, failure = 0;
    char *efasto, *msg;

    if(*pgpibDpvt->pdevGpibParmBlock->debugFlag>=2)
        printf("%s gpibWrite\n",precord->name);
    if(timeoutOccured) {
        pdevGpibPvt->finish(pgpibDpvt,1);
        return;
    }
    epicsMutexMustLock(pdeviceInterface->lock);
    if(pdeviceInstance->waitForSRQ) {
	ellAdd(&pdeviceInstance->waitList,&pgpibDpvt->node);
        epicsMutexUnlock(pdeviceInterface->lock);
	return;
    }
    epicsMutexUnlock(pdeviceInterface->lock);
    if (pgpibCmd->convert) {
        int cnvrtStat;
        cnvrtStat = pgpibCmd->convert(
            pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
        if(cnvrtStat==-1) failure = 1;
    }
    if(!failure) switch(cmdType) {
    case GPIBWRITE:
        if(!pgpibDpvt->msg) {
            printf("%s pgpibDpvt->msg is null\n",precord->name);
            recGblSetSevr(precord,READ_ALARM, INVALID_ALARM);
        } else {
            lenMessage = strlen(pgpibDpvt->msg);
	    nchars = writeIt(pgpibDpvt,pgpibDpvt->msg,lenMessage);
        }
	break;
    case GPIBCMD:
        if(!pgpibCmd->cmd) {
            printf("%s pgpibCmd->cmd is null\n",precord->name);
            recGblSetSevr(precord,READ_ALARM, INVALID_ALARM);
        } else {
            lenMessage = strlen(pgpibCmd->cmd);
	    nchars = writeIt(pgpibDpvt,pgpibCmd->cmd,lenMessage);
        }
	break;
    case GPIBACMD:
        if(!pasynGpib) {
            printf("%s gpibWrite got GPIBACMD but pasynGpib = 0"
               " record left with PACT true\n",precord->name);
            break;
        }
        if(!pgpibCmd->cmd) {
            printf("%s pgpibCmd->cmd is null\n",precord->name);
            recGblSetSevr(precord,READ_ALARM, INVALID_ALARM);
        } else {
            lenMessage = strlen(pgpibCmd->cmd);
            nchars = pasynGpib->addressedCmd(
                asynGpibPvt,pgpibDpvt->pasynUser,
                pgpibCmd->cmd,strlen(pgpibDpvt->msg));
        }
        break;
    case GPIBEFASTO:    /* write the enumerated cmd from the P3 array */
        /* bfr added: cmd is not ignored but evaluated as prefix to
         * pgpibCmd->P3[pgpibDpvt->efastVal] (cmd is _not_ intended to be an
         * independent command in itself). */
        if (pgpibCmd->P3[pgpibDpvt->efastVal] != NULL) {
            efasto = pgpibCmd->P3[pgpibDpvt->efastVal];
        } else {
            efasto = "";
        }
        if (pgpibCmd->cmd != NULL) {
            if(pgpibDpvt->msg
            && (pgpibCmd->msgLen > (strlen(efasto)+strlen(pgpibCmd->cmd)))) {
                printf(pgpibDpvt->msg, "%s%s", pgpibCmd->cmd, efasto);
            } else {
                recGblSetSevr(precord,READ_ALARM, INVALID_ALARM);
                printf("%s() no msg buffer or msgLen too small\n",precord->name);
                break;
            }
            msg = pgpibDpvt->msg;
        } else {
            msg = efasto;
        }
        lenMessage = msg ? strlen(pgpibDpvt->msg) : 0;
        if(lenMessage>0) {
	    nchars = writeIt(pgpibDpvt,pgpibDpvt->msg,lenMessage);
        } else {
            recGblSetSevr(precord,READ_ALARM, INVALID_ALARM);
            printf("%s msgLen is 0\n",precord->name);
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

static void queueCallback(asynUser *pasynUser)
{
    gpibDpvt *pgpibDpvt = (gpibDpvt *)pasynUser->userPvt;
    dbCommon *precord = pgpibDpvt->precord;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    deviceInterface *pdeviceInterface = pdevGpibPvt->pdeviceInterface;
    gpibWork work;
    int failure = 0;

    if(*pgpibDpvt->pdevGpibParmBlock->debugFlag>=2)
        printf("%s queueCallback\n",precord->name);
    epicsMutexMustLock(pdeviceInterface->lock);
    if(pdeviceInstance->timeoutActive) failure = isTimeWindowActive(pgpibDpvt);
    epicsMutexUnlock(pdeviceInterface->lock);
    if(!precord->pact) {
	printf("%s devSupportGpib:queueCallback but pact 0. Request ignored.\n",
            precord->name);
	return;
    }
    assert(pdevGpibPvt->work);
    work = pdevGpibPvt->work;
    pdevGpibPvt->work = 0;
    work(pgpibDpvt,failure);
}

static void timeoutCallback(asynUser *pasynUser)
{
    gpibDpvt *pgpibDpvt = (gpibDpvt *)pasynUser->userPvt;
    dbCommon *precord = pgpibDpvt->precord;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    deviceInterface *pdeviceInterface = pdevGpibPvt->pdeviceInterface;

    if(*pgpibDpvt->pdevGpibParmBlock->debugFlag)
        printf("%s timeoutCallback\n",precord->name);
    epicsMutexMustLock(pdeviceInterface->lock);
    if(pdeviceInstance->timeoutActive) isTimeWindowActive(pgpibDpvt);
    epicsMutexUnlock(pdeviceInterface->lock);
    if(!precord->pact) {
	printf("%s devSupportGpib:timeoutCallback but pact 0. Request ignored.\n",
            precord->name);
	return;
    }
    assert(pdevGpibPvt->work);
    (pdevGpibPvt->work)(pgpibDpvt,1);
    pdevGpibPvt->work = 0;
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
            queueIt(pdeviceInstance->pgpibDpvt,1);
	    /*Must unlock after queueIt to prevent possible race condition*/
	    pdeviceInstance->queueRequestFromSrq = 0;
            epicsMutexUnlock(pdeviceInterface->lock);
            return;
        } else if(pdeviceInstance->unsollicitedHandler) {
            epicsMutexUnlock(pdeviceInterface->lock);
	    pdeviceInstance->unsollicitedHandler(
                pdeviceInstance->unsollicitedHandlerPvt,gpibAddr,statusByte);
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
    return -1;
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
    pdeviceInstance->queueRequestFromSrq = 0;
    pgpibDpvtWait = (gpibDpvt *)ellFirst(&pdeviceInstance->waitList);
    while(pgpibDpvtWait) {
        gpibDpvt *pnext;
         
        pnext = (gpibDpvt *)ellNext(&pgpibDpvtWait->node);
        ellDelete(&pdeviceInstance->waitList,&pgpibDpvtWait->node);
	queueIt(pgpibDpvtWait,1);
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
    asynOctet *pasynOctet = pgpibDpvt->pasynOctet;
    void *asynOctetPvt = pgpibDpvt->asynOctetPvt;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    int respond2Writes = pgpibDpvt->pdevGpibParmBlock->respond2Writes;
    int nchars;

    if(*pgpibDpvt->pdevGpibParmBlock->debugFlag>=2)
        printf("%s writeIt message %s len %d\n",precord->name,message,len);
    nchars = pasynOctet->write(asynOctetPvt,pgpibDpvt->pasynUser,message,len);
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
            if(*pgpibDpvt->pdevGpibParmBlock->debugFlag>=2)
                printf("%s respond2Writes\n",precord->name);
            if(respond2Writes>0) epicsThreadSleep((double)(respond2Writes));
            nread = pasynOctet->read(asynOctetPvt,pgpibDpvt->pasynUser,rsp,len);
            if(nread!=len) {
                printf("%s respond2Writes but nread %d for %d length message\n",
                    precord->name,nread,len);
            }
        }
    }
    return(nchars);
}

static int isTimeWindowActive(gpibDpvt *pgpibDpvt)
{
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    epicsTimeStamp timeNow;
    double diff;
    int stillActive = 0;

    epicsTimeGetCurrent(&timeNow);
    diff = epicsTimeDiffInSeconds(&timeNow,&pdeviceInstance->timeoutTime);
    if(diff < pgpibDpvt->pdevGpibParmBlock->timeWindow) {
        stillActive = 1;
    }else {
        pdeviceInstance->timeoutActive = 0;
    }
    return(stillActive);
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
