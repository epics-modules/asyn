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
#define DEFAULT_SRQ_WAIT_TIMEOUT 5.0

typedef struct commonGpibPvt {
    ELLLIST portInstanceList;
    epicsTimerQueueId timerQueue;
}commonGpibPvt;
static commonGpibPvt *pcommonGpibPvt=0;

typedef struct deviceInstance {
    ELLNODE node; /*For portInstance.deviceInstanceList*/
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

typedef struct portInstance {
    ELLNODE node;   /*For commonGpibPvt.portInstanceList*/
    ELLLIST deviceInstanceList;
    epicsMutexId lock; 
    int link;
    char *portName;
    asynCommon *pasynCommon;
    void *asynCommonPvt;
    asynOctet *pasynOctet;
    void *asynOctetPvt;
    asynGpib *pasynGpib;
    void *asynGpibPvt;
    void *pupvt;                /* user defined pointer */
}portInstance;

struct devGpibPvt {
    portInstance *pportInstance;
    deviceInstance *pdeviceInstance;
    gpibWork work;
    gpibWork finish;
    char eos[2];
};

static long initRecord(dbCommon* precord, struct link * plink);
static long processGPIBSOFT(gpibDpvt *pgpibDpvt);
static void queueReadRequest(gpibDpvt *pgpibDpvt, gpibWork finish);
static void queueWriteRequest(gpibDpvt *pgpibDpvt, gpibWork finish);
static void queueRequest(gpibDpvt *pgpibDpvt, gpibWork work);
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
    registerSrqHandler,
    writeMsgLong,
    writeMsgULong,
    writeMsgDouble,
    writeMsgString
};
epicsShareDef devSupportGpib *pdevSupportGpib = &gpibSupport;

/*Initialization routines*/
static void commonGpibPvtInit(void);
static portInstance *createPortInstance(
    int link,asynUser *pasynUser,const char *portName);
static int getDeviceInstance(gpibDpvt *pgpibDpvt,int link,int gpibAddr);

/*Process routines */
static void queueIt(gpibDpvt *pgpibDpvt,int isLocked);
static void gpibPrepareToRead(gpibDpvt *pgpibDpvt,int failure);
static void gpibReadWaitComplete(gpibDpvt *pgpibDpvt,int failure);
static void gpibRead(gpibDpvt *pgpibDpvt,int failure);
static void gpibWrite(gpibDpvt *pgpibDpvt,int failure);

/*Callback routines*/
static void queueCallback(asynUser *pasynUser);
static void queueTimeoutCallback(asynUser *pasynUser);
static void srqHandlerGpib(void *parm, int gpibAddr, int statusByte);
static void srqWaitTimeoutCallback(void *parm);

/*Utility routines*/
/* gpibCmdIsConsistant returns (0,1) If (is not, is) consistant*/
static int gpibCmdIsConsistant(gpibDpvt *pgpibDpvt);
static int checkEnums(char * msg, char **enums);
static void gpibTimeoutHappened(gpibDpvt *pgpibDpvt);
static int isTimeWindowActive(gpibDpvt *pgpibDpvt);
static int writeIt(gpibDpvt *pgpibDpvt,char *message,int len);

/*iocsh  and dbior routines */
static void devGpibQueueTimeoutSet(
    const char *portName, int gpibAddr, double timeout);
static void devGpibSrqWaitTimeoutSet(
    const char *portName, int gpibAddr, double timeout);
static long report(int interest);

static long initRecord(dbCommon *precord, struct link *plink)
{
    gDset *pgDset = (gDset *)precord->dset;
    devGpibParmBlock *pdevGpibParmBlock = pgDset->pdevGpibParmBlock;
    gpibDpvt *pgpibDpvt;
    devGpibPvt *pdevGpibPvt;
    gpibCmd *pgpibCmd;
    portInstance *pportInstance;
    asynUser *pasynUser;
    int link,gpibAddr,parm;
    asynCommon *pasynCommon;
    asynOctet *pasynOctet;

    if(plink->type!=GPIB_IO) {
        printf("%s: init_record : GPIB link type %d is invalid",
            precord->name,plink->type);
        precord->pact = TRUE; /* keep record from being processed */
        return (-1);
    }
    link = plink->value.gpibio.link;
    gpibAddr = plink->value.gpibio.addr;
    sscanf(plink->value.gpibio.parm, "%d", &parm);
    pgpibDpvt = (gpibDpvt *) callocMustSucceed(1,
            (sizeof(gpibDpvt)+ sizeof(devGpibPvt)),"devSupportGpib");
    precord->dpvt = pgpibDpvt;
    pdevGpibPvt = (devGpibPvt *)(pgpibDpvt + 1);
    pgpibDpvt->pdevGpibPvt = pdevGpibPvt;
    pasynUser = pasynManager->createAsynUser(queueCallback,queueTimeoutCallback);
    pasynUser->userPvt = pgpibDpvt;
    pasynUser->timeout = pdevGpibParmBlock->timeout;
    pgpibDpvt->pdevGpibParmBlock = pdevGpibParmBlock;
    pgpibDpvt->pasynUser = pasynUser;
    pgpibDpvt->precord = precord;
    pgpibDpvt->parm = parm;
    if(getDeviceInstance(pgpibDpvt,link,gpibAddr)) {
        printf("%s: init_record : no driver for link %d\n",precord->name,link);
        precord->pact = TRUE; /* keep record from being processed */
        return(-1);
    }
    pgpibCmd = gpibCmdGet(pgpibDpvt);
    if (pgpibCmd->dset != (gDset *) precord->dset) {
        printf("%s : init_record : record type invalid for spec'd "
            "GPIB param#%d\n", precord->name,pgpibDpvt->parm);
        precord->pact = TRUE; /* keep record from being processed */
        return(-1);
    }
    pportInstance = pdevGpibPvt->pportInstance;
    pasynCommon = pportInstance->pasynCommon;
    pasynOctet = pportInstance->pasynOctet;
    if(!pasynCommon || !pasynOctet) {
        printf("%s: init_record : pasynCommon %p pasynOctet %p\n",
           precord->name,pgpibDpvt->pasynCommon,pgpibDpvt->pasynOctet);
        precord->pact = TRUE; /* keep record from being processed */
        return(-1);
    }
    pgpibDpvt->pasynCommon = pasynCommon;
    pgpibDpvt->asynCommonPvt = pportInstance->asynCommonPvt;
    pgpibDpvt->pasynOctet = pasynOctet;
    pgpibDpvt->asynOctetPvt = pportInstance->asynOctetPvt;
    pgpibDpvt->pasynGpib = pportInstance->pasynGpib;
    pgpibDpvt->asynGpibPvt = pportInstance->asynGpibPvt;
    if (pgpibCmd->msgLen > 0) {
        pgpibDpvt->msg = (char *)callocMustSucceed(
            pgpibCmd->msgLen,sizeof(char),"devSupportGpib");
    }
    if (pgpibCmd->rspLen > 0) {
        pgpibDpvt->rsp = (char *)callocMustSucceed(
            pgpibCmd->rspLen,sizeof(char),"devSupportGpib");
    }
    if(!gpibCmdIsConsistant(pgpibDpvt)) {
        precord->pact = TRUE; /* keep record from being processed */
        pasynManager->freeAsynUser(pasynUser);
        return(-1);
    }
    return(0);
}

static long processGPIBSOFT(gpibDpvt *pgpibDpvt)
{
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);

    if(!pgpibCmd->convert) return(-1);
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
    pdevGpibPvt->work = gpibPrepareToRead;
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

static void registerSrqHandler(gpibDpvt *pgpibDpvt,
    srqHandler handler,void *unsollicitedHandlerPvt)
{
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    dbCommon *precord = (dbCommon *)pgpibDpvt->precord;
    asynGpib *pasynGpib = pgpibDpvt->pasynGpib;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    portInstance *pportInstance = pdevGpibPvt->pportInstance;
    int failure=0;
    
    if(!pasynGpib) {
        printf("%s asynGpib not supported\n",precord->name);
        failure = -1;
    } else if(pdeviceInstance->unsollicitedHandler) {
        printf("%s an unsollicitedHandler already registered\n",precord->name);
        failure = -1;
    }
    if(failure==-1) {
        precord->pact = TRUE;
    }else {
        pdeviceInstance->unsollicitedHandlerPvt = unsollicitedHandlerPvt;
        pdeviceInstance->unsollicitedHandler = handler;
        if(!pdeviceInstance->waitForSRQ) {
            pportInstance->pasynGpib->pollAddr(
                pportInstance->asynGpibPvt,pgpibDpvt->pasynUser,1);
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
        recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM); \
        return(-1); \
    }\
    if(!pgpibCmd->format) {\
        printf("%s no format. Must define gpibCmd.format > 0.\n", \
            precord->name); \
        recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM); \
        return(-1); \
    }


#define writeMsgPostLog \
    if(nchars>pgpibCmd->msgLen) { \
        printf("%s msg buffer too small. msgLen %d message length %d\n", \
            precord->name,pgpibCmd->msgLen,nchars); \
        recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM); \
        return(-1); \
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
    int nchars; 
    dbCommon *precord = (dbCommon *)pgpibDpvt->precord; 
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    char *format = (pgpibCmd->format) ? pgpibCmd->format : "%s";

    if(!pgpibDpvt->msg) {
        printf("%s no msg buffer. Must define gpibCmd.msgLen > 0.\n", 
            precord->name);
        recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM); 
        return(-1); 
    }
    nchars = epicsSnprintf(pgpibDpvt->msg,pgpibCmd->msgLen,format,str);
    writeMsgPostLog
}

static void commonGpibPvtInit(void) 
{
    if(pcommonGpibPvt) return;
    pcommonGpibPvt = (commonGpibPvt *)callocMustSucceed(1,sizeof(commonGpibPvt),
        "devSupportGpib:commonGpibPvtInit");
    ellInit(&pcommonGpibPvt->portInstanceList);
    pcommonGpibPvt->timerQueue = epicsTimerQueueAllocate(
        1,epicsThreadPriorityScanLow);
}

static portInstance *createPortInstance(
    int link,asynUser *pasynUser,const char *portName)
{
    portInstance *pportInstance;
    asynInterface *pasynInterface;
    asynStatus status;
    int portNameSize;

    portNameSize = strlen(portName) + 1;
    pportInstance = (portInstance *)callocMustSucceed(
        1,sizeof(portInstance) + portNameSize,"devSupportGpib");
    ellInit(&pportInstance->deviceInstanceList);
    pportInstance->lock = epicsMutexMustCreate();
    pportInstance->link = link;
    pportInstance->portName = (char *)(pportInstance + 1);
    strcpy(pportInstance->portName,portName);
    pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,1);
    if(!pasynInterface) {
        printf("devSupportGpib: link %d %s not found\n",link,asynCommonType);
        free(pportInstance);
        return(0);
    }
    pportInstance->pasynCommon = (asynCommon *)pasynInterface->pinterface;
    pportInstance->asynCommonPvt = pasynInterface->drvPvt;
    pasynInterface = pasynManager->findInterface(pasynUser,asynOctetType,1);
    if(pasynInterface) {
        pportInstance->pasynOctet = 
            (asynOctet *)pasynInterface->pinterface;
        pportInstance->asynOctetPvt = pasynInterface->drvPvt;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynGpibType,1);
    if(pasynInterface) {
        pportInstance->pasynGpib = 
            (asynGpib *)pasynInterface->pinterface;
        pportInstance->asynGpibPvt = pasynInterface->drvPvt;
        status = pportInstance->pasynGpib->registerSrqHandler(
            pportInstance->asynGpibPvt,pasynUser,
            srqHandlerGpib,pportInstance);
        if(status!=asynSuccess) {
            printf("%s registerSrqHandler failed %s\n",
                portName,pasynUser->errorMessage);
        }
    }
    ellAdd(&pcommonGpibPvt->portInstanceList,&pportInstance->node);
    return(pportInstance);
}

static int getDeviceInstance(gpibDpvt *pgpibDpvt,int link,int gpibAddr)
{
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    char portName[80];
    portInstance *pportInstance;
    deviceInstance *pdeviceInstance;
    asynStatus status;
   
    if(!pcommonGpibPvt) commonGpibPvtInit();
    sprintf(portName,"L%d",link);
    status = pasynManager->connectDevice(pasynUser,portName,gpibAddr);
    if(status!=asynSuccess) {
       printf("devSupportGpib:getDeviceInstance link %d %s failed %s\n",
           link,portName,pasynUser->errorMessage);
       return(-1);
    }
    pportInstance = (portInstance *)
        ellFirst(&pcommonGpibPvt->portInstanceList);
    while(pportInstance) {
        if(link==pportInstance->link) break;
        pportInstance = (portInstance *)ellNext(&pportInstance->node);
    }
    if(!pportInstance) {
        pportInstance = createPortInstance(link,pasynUser,portName);
        if(!pportInstance) return(-1);
    }
    pdeviceInstance = (deviceInstance *)
        ellFirst(&pportInstance->deviceInstanceList);
    while(pdeviceInstance) {
        if(pdeviceInstance->gpibAddr == gpibAddr) break;
        pdeviceInstance = (deviceInstance *)ellNext(&pdeviceInstance->node);
    }
    if(!pdeviceInstance) {
        pdeviceInstance = (deviceInstance *)callocMustSucceed(
            1,sizeof(deviceInstance),"devSupportGpib");
        pdeviceInstance->gpibAddr = gpibAddr;
        pdeviceInstance->queueTimeout = DEFAULT_QUEUE_TIMEOUT;
        pdeviceInstance->srqWaitTimeout = DEFAULT_SRQ_WAIT_TIMEOUT;
        pdeviceInstance->srqWaitTimer = epicsTimerQueueCreateTimer(
            pcommonGpibPvt->timerQueue,srqWaitTimeoutCallback,pdeviceInstance);
        ellAdd(&pportInstance->deviceInstanceList,&pdeviceInstance->node);
    }
    pdevGpibPvt->pportInstance = pportInstance;
    pdevGpibPvt->pdeviceInstance = pdeviceInstance;
    return(0);
}

static void queueIt(gpibDpvt *pgpibDpvt,int isLocked)
{
    dbCommon *precord = pgpibDpvt->precord;
    devGpibParmBlock *pdevGpibParmBlock = pgpibDpvt->pdevGpibParmBlock;
    gpibCmd *pgpibCmd = &pdevGpibParmBlock->gpibCmds[pgpibDpvt->parm];
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    portInstance *pportInstance = pdevGpibPvt->pportInstance;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    asynStatus status;

    if(!isLocked)epicsMutexMustLock(pportInstance->lock);
    if(pdeviceInstance->timeoutActive) {
        if(isTimeWindowActive(pgpibDpvt)) {
            recGblSetSevr(precord, SOFT_ALARM, INVALID_ALARM);
            if(!isLocked)epicsMutexUnlock(pportInstance->lock);
            printf("%s queueRequest failed timeWindow active\n",
                precord->name);
            return;
        }
    }
    precord->pact = TRUE;
    status = pasynManager->queueRequest(pgpibDpvt->pasynUser,
        pgpibCmd->pri,pdeviceInstance->queueTimeout);
    if(status!=asynSuccess) {
        precord->pact = FALSE;
        recGblSetSevr(precord, SOFT_ALARM, INVALID_ALARM);
        if(!isLocked)epicsMutexUnlock(pportInstance->lock);
        printf("%s queueRequest failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
        return;
    }
    if(!isLocked)epicsMutexUnlock(pportInstance->lock);
}

static void gpibPrepareToRead(gpibDpvt *pgpibDpvt,int failure)
{
    dbCommon *precord = pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdTypeNoEOS(pgpibCmd->type);
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    portInstance *pportInstance = pdevGpibPvt->pportInstance;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    int nchars = 0, lenmsg = 0, lenEos = 0;;
    asynStatus status;

    if(*pgpibDpvt->pdevGpibParmBlock->debugFlag>=2)
        printf("%s gpibPrepareToRead\n",precord->name);
    if(failure) {
        recGblSetSevr(precord,READ_ALARM, INVALID_ALARM);
        gpibRead(pgpibDpvt,-1); return;
    }
    epicsMutexMustLock(pportInstance->lock);
    /*Since queueReadRequest calls lock waitForSRQ should not be true*/
    assert(!pdeviceInstance->waitForSRQ);
    if(cmdType&(GPIBREADW|GPIBEFASTIW)) {
        pdeviceInstance->waitForSRQ = 1;
        pdeviceInstance->pgpibDpvt = pgpibDpvt;
        if(!pdeviceInstance->unsollicitedHandler) {
            pportInstance->pasynGpib->pollAddr(
                pportInstance->asynGpibPvt,pgpibDpvt->pasynUser,1);
        }
        pdevGpibPvt->work = gpibReadWaitComplete;
        epicsTimerStartDelay(pdeviceInstance->srqWaitTimer,
            pdeviceInstance->srqWaitTimeout);
    }
    epicsMutexUnlock(pportInstance->lock);
    if(pgpibCmd->type&GPIBEOS) {
        pdevGpibPvt->eos[0] = (char)pgpibCmd->eosChar;
        lenEos = 1;
    } else {
        lenEos = 0;
    }
    status = pgpibDpvt->pasynOctet->setEos(
        pgpibDpvt->asynOctetPvt,pgpibDpvt->pasynUser,pdevGpibPvt->eos,1);
    if(status!=asynSuccess) {
        printf("%s pasynOctet->setEos failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
    }
    switch(cmdType) {
    case GPIBREADW:
    case GPIBEFASTIW:
    case GPIBREAD:
    case GPIBEFASTI:
        if(!pgpibCmd->cmd) {
            printf("%s pgpibCmd->cmd is null\n",precord->name);
            recGblSetSevr(precord,READ_ALARM, INVALID_ALARM);
            gpibRead(pgpibDpvt,-1); return;
        }
        lenmsg = strlen(pgpibCmd->cmd);
        nchars = writeIt(pgpibDpvt,pgpibCmd->cmd,lenmsg);
        if(nchars!=lenmsg) {
            if(cmdType&(GPIBREADW|GPIBEFASTIW)) {
                epicsTimerCancel(pdeviceInstance->srqWaitTimer);
                /* simulate  srqWaitTimeout so that proper cleanup is done*/
                gpibReadWaitComplete(pgpibDpvt,-1); return;
            }
            recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
            pdevGpibPvt->finish(pgpibDpvt,-1);
            break;
        }
        if(cmdType&(GPIBREADW|GPIBEFASTIW)) return;
    case GPIBRAWREAD:
        gpibRead(pgpibDpvt,0); return;
        break;
    default:
        printf("%s gpibPrepareToRead can't handle cmdType %d"
               " record left with PACT true\n",precord->name,cmdType);
    }
}

static void gpibReadWaitComplete(gpibDpvt *pgpibDpvt,int failure)
{
    dbCommon *precord = pgpibDpvt->precord;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    portInstance *pportInstance = pdevGpibPvt->pportInstance;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;

    if(*pgpibDpvt->pdevGpibParmBlock->debugFlag>=2)
        printf("%s gpibReadWaitComplete\n",precord->name);
    epicsMutexMustLock(pportInstance->lock);
    /*check that pgpibDpvt is owner of waitForSRQ*/
    if(!pdeviceInstance->waitForSRQ) {
        epicsMutexUnlock(pportInstance->lock);
        return;
    }
    assert(pdeviceInstance->pgpibDpvt==pgpibDpvt);
    if(!pdeviceInstance->unsollicitedHandler) {
        pportInstance->pasynGpib->pollAddr(
            pportInstance->asynGpibPvt,pgpibDpvt->pasynUser,0);
    }
    pdeviceInstance->waitForSRQ = 0;
    pdeviceInstance->pgpibDpvt = 0;
    pdeviceInstance->queueRequestFromSrq = 0;
    epicsMutexUnlock(pportInstance->lock);
    gpibRead(pgpibDpvt,failure);
}

static void gpibRead(gpibDpvt *pgpibDpvt,int failure)
{
    dbCommon *precord = pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdTypeNoEOS(pgpibCmd->type);
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    asynOctet *pasynOctet = pgpibDpvt->pasynOctet;
    void *asynOctetPvt = pgpibDpvt->asynOctetPvt;
    int nchars;
    asynStatus status;

    if(failure) goto done;
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
    pgpibDpvt->msgInputLen = nchars;
    if(nchars==0) {
        gpibTimeoutHappened(pgpibDpvt);
        recGblSetSevr(precord,READ_ALARM, INVALID_ALARM);
        failure = -1; goto done;
    }
    if(nchars<pgpibCmd->msgLen) pgpibDpvt->msg[nchars] = 0;
    if(cmdType&(GPIBEFASTI|GPIBEFASTIW)) 
        pgpibDpvt->efastVal = checkEnums(pgpibDpvt->msg, pgpibCmd->P3);
done:
    pdevGpibPvt->finish(pgpibDpvt,failure);
    status = pasynManager->unlock(pgpibDpvt->pasynUser);
    if(status!=asynSuccess) {
        printf("%s pasynManager->unlock failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
    }
}

static void gpibWrite(gpibDpvt *pgpibDpvt,int failure)
{
    dbCommon *precord = pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    asynGpib *pasynGpib = pgpibDpvt->pasynGpib;
    void *asynGpibPvt = pgpibDpvt->asynGpibPvt;
    int cmdType = gpibCmdTypeNoEOS(pgpibCmd->type);
    int nchars = 0, lenMessage = 0;
    char *efasto, *msg;

    if(*pgpibDpvt->pdevGpibParmBlock->debugFlag>=2)
        printf("%s gpibWrite\n",precord->name);
    if(failure) {
        recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
        pdevGpibPvt->finish(pgpibDpvt,-1);
        return;
    }
    if (pgpibCmd->convert) {
        int cnvrtStat;
        cnvrtStat = pgpibCmd->convert(
            pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
        if(cnvrtStat==-1) {
            failure = -1;
        } else {
            lenMessage = cnvrtStat;
        }
    }
    if(!failure) switch(cmdType) {
    case GPIBWRITE:
        if(!pgpibDpvt->msg) {
            printf("%s pgpibDpvt->msg is null\n",precord->name);
            recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
        } else {
            if(lenMessage==0) lenMessage = strlen(pgpibDpvt->msg);
            nchars = writeIt(pgpibDpvt,pgpibDpvt->msg,lenMessage);
        }
        break;
    case GPIBCMD:
        if(!pgpibCmd->cmd) {
            printf("%s pgpibCmd->cmd is null\n",precord->name);
            recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
        } else {
            if(lenMessage==0) lenMessage = strlen(pgpibCmd->cmd);
            nchars = writeIt(pgpibDpvt,pgpibCmd->cmd,lenMessage);
        }
        break;
    case GPIBACMD:
        if(!pasynGpib) {
            printf("%s gpibWrite got GPIBACMD but pasynGpib 0\n",precord->name);
            break;
        }
        if(!pgpibCmd->cmd) {
            printf("%s pgpibCmd->cmd is null\n",precord->name);
            recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
        } else {
            if(lenMessage==0) lenMessage = strlen(pgpibCmd->cmd);
            nchars = pasynGpib->addressedCmd(
                asynGpibPvt,pgpibDpvt->pasynUser,
                pgpibCmd->cmd,strlen(pgpibDpvt->msg));
        }
        break;
    case GPIBEFASTO:    /* write the enumerated cmd from the P3 array */
        /* bfr added: cmd is not ignored but evaluated as prefix to
         * pgpibCmd->P3[pgpibDpvt->efastVal] (cmd is _not_ intended to be an
         * independent command in itself). */
        if(pgpibCmd->P1<=pgpibDpvt->efastVal) {
            recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
            printf("%s() efastVal out of range\n",precord->name);
            break;
        }
        efasto = pgpibCmd->P3[pgpibDpvt->efastVal];
        if (pgpibCmd->cmd != NULL) {
            if(pgpibDpvt->msg
            && (pgpibCmd->msgLen > (strlen(efasto)+strlen(pgpibCmd->cmd)))) {
                sprintf(pgpibDpvt->msg, "%s%s", pgpibCmd->cmd, efasto);
            } else {
                recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
                printf("%s() no msg buffer or msgLen too small\n",precord->name);
                break;
            }
            msg = pgpibDpvt->msg;
        } else {
            msg = efasto;
        }
        lenMessage = msg ? strlen(msg) : 0;
        if(lenMessage>0) {
            nchars = writeIt(pgpibDpvt,msg,lenMessage);
        } else {
            recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
            printf("%s msgLen is 0\n",precord->name);
        }
        break;
    default:
        printf("%s gpibWrite cant handle cmdType %d"
               " record left with PACT true\n",precord->name,cmdType);
        return;
    }
    if(nchars!=lenMessage) failure = -1;
    if(failure) recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
    pdevGpibPvt->finish(pgpibDpvt,failure);
}

static void queueCallback(asynUser *pasynUser)
{
    gpibDpvt *pgpibDpvt = (gpibDpvt *)pasynUser->userPvt;
    dbCommon *precord = pgpibDpvt->precord;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    portInstance *pportInstance = pdevGpibPvt->pportInstance;
    gpibWork work;
    int failure = 0;

    if(*pgpibDpvt->pdevGpibParmBlock->debugFlag>=2)
        printf("%s queueCallback\n",precord->name);
    epicsMutexMustLock(pportInstance->lock);
    if(pdeviceInstance->timeoutActive)
        failure = isTimeWindowActive(pgpibDpvt) ? -1 : 0;
    if(!precord->pact) {
        epicsMutexUnlock(pportInstance->lock);
        printf("%s devSupportGpib:queueCallback but pact 0. Request ignored.\n",
            precord->name);
        return;
    }
    assert(pdevGpibPvt->work);
    work = pdevGpibPvt->work;
    pdevGpibPvt->work = 0;
    epicsMutexUnlock(pportInstance->lock);
    work(pgpibDpvt,failure);
}

static void queueTimeoutCallback(asynUser *pasynUser)
{
    gpibDpvt *pgpibDpvt = (gpibDpvt *)pasynUser->userPvt;
    dbCommon *precord = pgpibDpvt->precord;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    portInstance *pportInstance = pdevGpibPvt->pportInstance;
    gpibWork work;

    if(*pgpibDpvt->pdevGpibParmBlock->debugFlag)
        printf("%s queueTimeoutCallback\n",precord->name);
    epicsMutexMustLock(pportInstance->lock);
    if(pdeviceInstance->timeoutActive) isTimeWindowActive(pgpibDpvt);
    if(!precord->pact) {
        epicsMutexUnlock(pportInstance->lock);
        printf("%s devSupportGpib:queueTimeoutCallback but pact 0. "
            "Request ignored.\n", precord->name);
        return;
    }
    assert(pdevGpibPvt->work);
    work = pdevGpibPvt->work;
    pdevGpibPvt->work = 0;
    epicsMutexUnlock(pportInstance->lock);
    work(pgpibDpvt,-1);
}

static void srqHandlerGpib(void *parm, int gpibAddr, int statusByte)
{
    portInstance *pportInstance = (portInstance *)parm;
    deviceInstance *pdeviceInstance;

    epicsMutexMustLock(pportInstance->lock);
    pdeviceInstance = (deviceInstance *)ellFirst(
        &pportInstance->deviceInstanceList);
    while(pdeviceInstance) {
        if(pdeviceInstance->gpibAddr!=gpibAddr) {
            pdeviceInstance = (deviceInstance *)ellNext(&pdeviceInstance->node);
            continue;
        }
        if(pdeviceInstance->waitForSRQ) {
            pdeviceInstance->queueRequestFromSrq = 1;
            epicsTimerCancel(pdeviceInstance->srqWaitTimer);
            queueIt(pdeviceInstance->pgpibDpvt,1);
            /*Must unlock after queueIt to prevent possible race condition*/
            epicsMutexUnlock(pportInstance->lock);
            return;
        } else if(pdeviceInstance->unsollicitedHandler) {
            epicsMutexUnlock(pportInstance->lock);
            pdeviceInstance->unsollicitedHandler(
                pdeviceInstance->unsollicitedHandlerPvt,gpibAddr,statusByte);
            return;
        }
        break;
    }
    epicsMutexUnlock(pportInstance->lock);
    printf("portName %s link %d gpibAddr %d "
           "SRQ happened but no record is attached to the gpibAddr\n",
            pportInstance->portName,pportInstance->link,gpibAddr);
}

static void srqWaitTimeoutCallback(void *parm)
{
    deviceInstance *pdeviceInstance = (deviceInstance *)parm;
    gpibDpvt *pgpibDpvt = pdeviceInstance->pgpibDpvt;
    devGpibPvt *pdevGpibPvt;
    portInstance *pportInstance;

    pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    pportInstance = pdevGpibPvt->pportInstance;
    epicsMutexMustLock(pportInstance->lock);
    /*Check that SRQ did not occur after timeout started*/
    if(!pdeviceInstance->pgpibDpvt || pdeviceInstance->queueRequestFromSrq) {
        epicsMutexUnlock(pportInstance->lock);
        return;
    }
    assert(pdeviceInstance->waitForSRQ);
    epicsMutexUnlock(pportInstance->lock);
    gpibReadWaitComplete(pgpibDpvt,-1);
}

static int gpibCmdIsConsistant(gpibDpvt *pgpibDpvt)
{
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt); dbCommon *precord = pgpibDpvt->precord; 
    /* If gpib spscific command make sure that pasynGpib is available*/
    if(!pgpibDpvt->pasynGpib) {
        if(pgpibCmd->type&(
        GPIBACMD|GPIBREADW|GPIBEFASTIW|GPIBIFC
        |GPIBREN|GPIBDCL|GPIBLLO|GPIBSDC|GPIBGTL|GPIBSRQHANDLER)) {
            printf("%s parm %d gpibCmd.type requires asynGpib but "
                "it is not implemented by port driver\n",
                 precord->name,pgpibDpvt->parm);
            return(0);
        }
    }
    if(pgpibCmd->type&GPIBSOFT && !pgpibCmd->convert) {
        printf("%s parm %d GPIBSOFT but convert is null\n",
            precord->name,pgpibDpvt->parm);
        return(0);
    }
    if(pgpibCmd->type&(GPIBEFASTO|GPIBEFASTI|GPIBEFASTIW)) {
        /*Set P1 = number of items in efast table */
        int n = 0;
        if(pgpibCmd->P3) {
            char **enums = pgpibCmd->P3;
            while(enums[n] !=0) n++;
        }
        pgpibCmd->P1 = n;
        if(n==0) {
            printf("%s parm %d P3 must be an EFAST table\n",
                precord->name,pgpibDpvt->parm);
            return(0);
        }
    }
    if(pgpibCmd->type&(GPIBREAD|GPIBREADW)) {
        if(!pgpibCmd->cmd) {
            printf("%s parm %d requires cmd\n",
                precord->name,pgpibDpvt->parm);
            return(0);
        }
    }
    if(pgpibCmd->type&(GPIBCMD|GPIBACMD)) {
        if(!pgpibCmd->cmd) {
            printf("%s parm %d requires cmd \n",
                precord->name,pgpibDpvt->parm);
            return(0);
        }
    }
    return (1);
}

static int checkEnums(char *msg, char **enums)
{
    int i = 0;

    if(!enums) return -1;
    while (enums[i] != 0) {
        int j = 0;
        while(enums[i][j] && (enums[i][j] == msg[j]) ) j++;
        if (enums[i][j] == 0) return (i);
        i++;
    }
    return -1;
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
            gpibTimeoutHappened(pgpibDpvt);
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

static void gpibTimeoutHappened(gpibDpvt *pgpibDpvt)
{
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    portInstance *pportInstance = pdevGpibPvt->pportInstance;

    epicsMutexMustLock(pportInstance->lock);
    pdeviceInstance->timeoutActive = TRUE;
    epicsTimeGetCurrent(&pdeviceInstance->timeoutTime);
    ++pdeviceInstance->tmoCount;
    epicsMutexUnlock(pportInstance->lock);
}

static int isTimeWindowActive(gpibDpvt *pgpibDpvt)
{
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    portInstance *pportInstance = pdevGpibPvt->pportInstance;
    epicsTimeStamp timeNow;
    double diff;
    int stillActive = 0;

    epicsTimeGetCurrent(&timeNow);
    epicsMutexMustLock(pportInstance->lock);
    diff = epicsTimeDiffInSeconds(&timeNow,&pdeviceInstance->timeoutTime);
    if(diff < pgpibDpvt->pdevGpibParmBlock->timeWindow) {
        stillActive = 1;
    }else {
        pdeviceInstance->timeoutActive = 0;
    }
    epicsMutexUnlock(pportInstance->lock);
    return(stillActive);
}

#define devGpibDeviceInterfaceSetCommon \
    portInstance  *pportInstance;\
    deviceInstance *pdeviceInstance;\
\
    if(!pcommonGpibPvt) commonGpibPvtInit();\
    pportInstance = (portInstance *)ellFirst(\
        &pcommonGpibPvt->portInstanceList);\
    while(pportInstance) {\
        if(strcmp(portName,pportInstance->portName)==0) break;\
        pportInstance = (portInstance *)ellNext(&pportInstance->node);\
    }\
    if(!pportInstance) {\
        printf("%s no found\n",portName);\
        return;\
    }\
    pdeviceInstance = (deviceInstance *)ellFirst(\
         &pportInstance->deviceInstanceList);\
    while(pdeviceInstance) {\
        if(gpibAddr==pdeviceInstance->gpibAddr) break;\
        pdeviceInstance = (deviceInstance *)ellNext(&pdeviceInstance->node);\
    }\
    if(!pdeviceInstance) {\
        printf("gpibAddr %d not found\n",gpibAddr);\
        return;\
    }

static void devGpibQueueTimeoutSet(
    const char *portName, int gpibAddr, double timeout)
{
    devGpibDeviceInterfaceSetCommon

    pdeviceInstance->queueTimeout = timeout;
}

static void devGpibSrqWaitTimeoutSet(
    const char *portName, int gpibAddr, double timeout)
{
    devGpibDeviceInterfaceSetCommon

    pdeviceInstance->srqWaitTimeout = timeout;
}

/* Support for dbior */
static long report(int interest)
{
    asynUser *pasynUser;
    portInstance  *pportInstance;
    deviceInstance *pdeviceInstance;

    if(!pcommonGpibPvt) commonGpibPvtInit();
    pasynUser = pasynManager->createAsynUser(0,0);
    pportInstance = (portInstance *)ellFirst(
        &pcommonGpibPvt->portInstanceList);
    while(pportInstance) {
        printf("link %d portName %s\n",
            pportInstance->link,pportInstance->portName);
        printf("    pasynCommon %p pasynOctet %p pasynGpib %p\n",
            pportInstance->pasynCommon,pportInstance->pasynOctet,
            pportInstance->pasynGpib);
        if(pportInstance->pasynCommon) {
            pportInstance->pasynCommon->report(
                pportInstance->asynCommonPvt,stdout,interest);
        }
        pdeviceInstance = (deviceInstance *)ellFirst(
            &pportInstance->deviceInstanceList);
        while(pdeviceInstance) {
            printf("    gpibAddr %d timeouts %lu errors %lu "
                   "queueTimeout %f srqWaitTimeout %f\n",
                pdeviceInstance->gpibAddr,
                pdeviceInstance->tmoCount,pdeviceInstance->errorCount,
                pdeviceInstance->queueTimeout,pdeviceInstance->srqWaitTimeout);
            pdeviceInstance = (deviceInstance *)ellNext(&pdeviceInstance->node);
        }
        pportInstance = (portInstance *)ellNext(&pportInstance->node);
    }
    pasynManager->freeAsynUser(pasynUser);
    return(0);
}

static gDset devGpibReport = {
    6,
    {report,0,0,0,0,0},
    0
};
epicsExportAddress(dset,devGpibReport);

static const iocshArg devGpibReportArg0 = {"level", iocshArgInt};
static const iocshArg *const devGpibReportArgs[1] = {&devGpibReportArg0};
static const iocshFuncDef devGpibReportDef =
    {"devGpibReport", 1, devGpibReportArgs};
static void devGpibReportCall(const iocshArgBuf * args) {
        report(args[0].ival);
}

static const iocshArg devGpibQueueTimeoutArg0 =
 {"portName",iocshArgString};
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
 {"portName",iocshArgString};
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
