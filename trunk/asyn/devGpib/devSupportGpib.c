/* devSupportGpib.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
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
    /* Following field is used for registerSrqHandlerCallback*/
    asynUser *pasynUser;
    /*The following are for shared msg buffer*/
    char *msg;   /*shared msg buffer for all gpibCmds*/
    int  msgLen;/*size of msg*/
    int  msgLenMax; /*msgLenMax all devices attached to this port*/
    char *rsp;   /*shared rsp buffer for all respond2Writes*/
    int  rspLen;/*size of rsp*/
    int  rspLenMax; /*rspLenMax all devices attached to this port*/
}portInstance;

struct devGpibPvt {
    portInstance *pportInstance;
    deviceInstance *pdeviceInstance;
    gpibWork work;
    gpibWork start;
    gpibWork finish;
};

static long initRecord(dbCommon* precord, struct link * plink);
static void processGPIBSOFT(gpibDpvt *pgpibDpvt);
static void queueReadRequest(gpibDpvt *pgpibDpvt,gpibWork start,gpibWork finish);
static void queueWriteRequest(gpibDpvt *pgpibDpvt,gpibWork start,gpibWork finish);
static void queueRequest(gpibDpvt *pgpibDpvt, gpibWork work);
static void registerSrqHandler(gpibDpvt *pgpibDpvt,
    srqHandler handler,void *unsollicitedHandlerPvt);
static int writeMsgLong(gpibDpvt *pgpibDpvt,long val);
static int writeMsgULong(gpibDpvt *pgpibDpvt,unsigned long val);
static int writeMsgDouble(gpibDpvt *pgpibDpvt,double val);
static int writeMsgString(gpibDpvt *pgpibDpvt,const char *str);
static int readArbitraryBlockProgramData(gpibDpvt *pgpibDpvt);

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
    writeMsgString,
    readArbitraryBlockProgramData
};
epicsShareDef devSupportGpib *pdevSupportGpib = &gpibSupport;

/*Initialization routines*/
static void commonGpibPvtInit(void);
static void setMsgRsp(gpibDpvt *pgpibDpvt);
static void registerSrqHandlerCallback(asynUser *pasynUser);
static portInstance *createPortInstance(
    int link,asynUser *pasynUser,const char *portName);
static int getDeviceInstance(gpibDpvt *pgpibDpvt,int link,int gpibAddr);

/*Process routines */
static void queueIt(gpibDpvt *pgpibDpvt,int isLocked);
static int gpibPrepareToRead(gpibDpvt *pgpibDpvt,int failure);
static int gpibReadWaitComplete(gpibDpvt *pgpibDpvt,int failure);
static int gpibRead(gpibDpvt *pgpibDpvt,int failure);
static int gpibWrite(gpibDpvt *pgpibDpvt,int failure);

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
static long init(int pass);
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
        return -1;
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
        return -1;
    }
    pgpibCmd = gpibCmdGet(pgpibDpvt);
    if (pgpibCmd->dset != (gDset *) precord->dset) {
        printf("%s : init_record : record type invalid for spec'd "
            "GPIB param#%d\n", precord->name,pgpibDpvt->parm);
        precord->pact = TRUE; /* keep record from being processed */
        return -1;
    }
    pportInstance = pdevGpibPvt->pportInstance;
    pasynCommon = pportInstance->pasynCommon;
    pasynOctet = pportInstance->pasynOctet;
    if(!pasynCommon || !pasynOctet) {
        printf("%s: init_record : pasynCommon %p pasynOctet %p\n",
           precord->name,pgpibDpvt->pasynCommon,pgpibDpvt->pasynOctet);
        precord->pact = TRUE; /* keep record from being processed */
        return -1;
    }
    pgpibDpvt->pasynCommon = pasynCommon;
    pgpibDpvt->asynCommonPvt = pportInstance->asynCommonPvt;
    pgpibDpvt->pasynOctet = pasynOctet;
    pgpibDpvt->asynOctetPvt = pportInstance->asynOctetPvt;
    pgpibDpvt->pasynGpib = pportInstance->pasynGpib;
    pgpibDpvt->asynGpibPvt = pportInstance->asynGpibPvt;
    setMsgRsp(pgpibDpvt);
    if(!gpibCmdIsConsistant(pgpibDpvt)) {
        precord->pact = TRUE; /* keep record from being processed */
        pasynManager->freeAsynUser(pasynUser);
        return -1;
    }
    return 0;
}

static void processGPIBSOFT(gpibDpvt *pgpibDpvt)
{
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    dbCommon *precord = pgpibDpvt->precord;
    int status = 0;

    if(!pgpibCmd->convert) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s processGPIBSOFT but no convert\n",precord->name);
        recGblSetSevr(pgpibDpvt->precord,READ_ALARM,INVALID_ALARM);
        return;
    }
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s processGPIBSOFT\n",precord->name);
    pasynUser->errorMessage[0] = 0;
    status = pgpibCmd->convert(pgpibDpvt,pgpibCmd->P1,pgpibCmd->P2,pgpibCmd->P3);
    if(status) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s convert failed %s\n",
            precord->name,pasynUser->errorMessage);
        recGblSetSevr(pgpibDpvt->precord,READ_ALARM,INVALID_ALARM);
    }
    return;
}

static void queueReadRequest(gpibDpvt *pgpibDpvt,gpibWork start,gpibWork finish)
{
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    dbCommon *precord = pgpibDpvt->precord;
    asynStatus status;

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s queueReadRequest\n",precord->name);
    pdevGpibPvt->work = gpibPrepareToRead;
    pdevGpibPvt->start = start;
    pdevGpibPvt->finish = finish;
    status = pasynManager->lock(pgpibDpvt->pasynUser);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s pasynManager->lock failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
        recGblSetSevr(precord, SOFT_ALARM, INVALID_ALARM);
        return;
    }
    queueIt(pgpibDpvt,0);
}

static void queueWriteRequest(gpibDpvt *pgpibDpvt,gpibWork start,gpibWork finish)
{
    dbCommon *precord = pgpibDpvt->precord;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s queueWriteRequest\n",precord->name);
    pdevGpibPvt->work = gpibWrite;
    pdevGpibPvt->start = start;
    pdevGpibPvt->finish = finish;
    queueIt(pgpibDpvt,0);
}

static void queueRequest(gpibDpvt *pgpibDpvt, gpibWork work)
{
    dbCommon *precord = pgpibDpvt->precord;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s queueRequest\n",precord->name);
    pdevGpibPvt->work = work;
    pdevGpibPvt->finish = 0;
    queueIt(pgpibDpvt,0);
}

static void registerSrqHandler(gpibDpvt *pgpibDpvt,
    srqHandler handler,void *unsollicitedHandlerPvt)
{
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    dbCommon *precord = (dbCommon *)pgpibDpvt->precord;
    asynGpib *pasynGpib = pgpibDpvt->pasynGpib;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    portInstance *pportInstance = pdevGpibPvt->pportInstance;
    int failure=0;
    
    if(!pasynGpib) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s asynGpib not supported\n",precord->name);
        failure = -1;
    } else if(pdeviceInstance->unsollicitedHandler) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s an unsollicitedHandler already registered\n",precord->name);
        failure = -1;
    }
    if(failure==-1) {
        precord->pact = TRUE;
    }else {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s registerSrqHandler\n",precord->name);
        pdeviceInstance->unsollicitedHandlerPvt = unsollicitedHandlerPvt;
        pdeviceInstance->unsollicitedHandler = handler;
        if(!pdeviceInstance->waitForSRQ) {
            pportInstance->pasynGpib->pollAddr(
                pportInstance->asynGpibPvt,pgpibDpvt->pasynUser,1);
        }
    }
}

#define writeMsgProlog \
    asynUser *pasynUser = pgpibDpvt->pasynUser; \
    int nchars; \
    dbCommon *precord = (dbCommon *)pgpibDpvt->precord; \
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt); \
    if(!pgpibDpvt->msg) { \
        asynPrint(pasynUser,ASYN_TRACE_ERROR, \
            "%s no msg buffer. Must define gpibCmd.msgLen > 0.\n", \
            precord->name); \
        recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM); \
        return -1; \
    }\
    if(!pgpibCmd->format) {\
        asynPrint(pasynUser,ASYN_TRACE_ERROR, \
            "%s no format. Must define gpibCmd.format > 0.\n", \
            precord->name); \
        recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM); \
        return -1; \
    }


#define writeMsgPostLog \
    if(nchars>pgpibCmd->msgLen) { \
        asynPrint(pasynUser,ASYN_TRACE_ERROR, \
            "%s msg buffer too small. msgLen %d message length %d\n", \
            precord->name,pgpibCmd->msgLen,nchars); \
        recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM); \
        return -1; \
    } \
    return 0;

static int writeMsgLong(gpibDpvt *pgpibDpvt,long val)
{
    writeMsgProlog
    nchars = epicsSnprintf(pgpibDpvt->msg,pgpibCmd->msgLen,pgpibCmd->format,val);
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s writeMsgLong\n",precord->name);
    writeMsgPostLog
}

static int writeMsgULong(gpibDpvt *pgpibDpvt,unsigned long val)
{
    writeMsgProlog
    nchars = epicsSnprintf(pgpibDpvt->msg,pgpibCmd->msgLen,pgpibCmd->format,val);
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s writeMsgULong\n",precord->name);
    writeMsgPostLog
}

static int writeMsgDouble(gpibDpvt *pgpibDpvt,double val)
{
    writeMsgProlog
    nchars = epicsSnprintf(pgpibDpvt->msg,pgpibCmd->msgLen,pgpibCmd->format,val);
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s writeMsgDouble\n",precord->name);
    writeMsgPostLog
}

static int writeMsgString(gpibDpvt *pgpibDpvt,const char *str)
{
    asynUser *pasynUser = pgpibDpvt->pasynUser; 
    int nchars; 
    dbCommon *precord = (dbCommon *)pgpibDpvt->precord; 
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    char *format = (pgpibCmd->format) ? pgpibCmd->format : "%s";

    if(!pgpibDpvt->msg) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s no msg buffer. Must define gpibCmd.msgLen > 0.\n", 
            precord->name);
        recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM); 
        return -1; 
    }
    nchars = epicsSnprintf(pgpibDpvt->msg,pgpibCmd->msgLen,format,str);
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s writeMsgString\n",precord->name);
    writeMsgPostLog
}

/*
 * Read IEEE-488.2 Arbitrary Block Program Data.
 * Allows arbitrary data to be read from serial line.
 */
static int readArbitraryBlockProgramData(gpibDpvt *pgpibDpvt)
{
    long ltmp;
    int nread;
    int count;
    char *endptr;
    asynOctet *pasynOctet = pgpibDpvt->pasynOctet;
    void *asynOctetPvt = pgpibDpvt->asynOctetPvt;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    char *buf = pgpibDpvt->msg;
    int bufSize = pgpibCmd->msgLen;

    pasynOctet->setEos(asynOctetPvt,pasynUser,"#",1);
    nread = pasynOctet->read(asynOctetPvt,pasynUser,buf,bufSize);
    if (nread <= 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                      "Error reading preamble");
        return -1;
    }
    if (buf[nread - 1] != '#') {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                           "Preamble too long");
        return -1;
    }
    buf += nread;
    bufSize -= nread;
    pasynOctet->setEos(asynOctetPvt,pasynUser,NULL,0);
    nread = pasynOctet->read(asynOctetPvt,pasynUser,buf,1);
    if (nread < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                              "Error reading number of digits");
        return -1;
    }
    if ((buf[0] <'0') || (buf[0] > '9')) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "Number of digits ('\\%.2x') is not numeric",(unsigned char)buf[0]);
        return -1;
    }
    count = buf[0] - '0';
    buf += nread;
    bufSize -= nread;
    if (count == 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                    "Number of digits is zero");
        return -1;
    }
    if (count > bufSize) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                            "Buffer too small");
        return -1;
    }
    nread = pasynOctet->read(asynOctetPvt,pasynUser,buf,count);
    if (nread < 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                               "Error reading number of bytes");
        return -1;
    }
    buf[nread] = '\0';
    ltmp = strtol(buf,&endptr,10);
    if ((endptr == buf) || (*endptr != '\0')) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                     "Number of bytes (%s) is not numeric",buf);
        return -1;
    }
    if ((ltmp <= 0) || (ltmp >= bufSize)) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                  "Number of bytes (%lu) is unreasonable",ltmp);
        return -1;
    }
    buf += nread;
    bufSize -= nread;
    count = ltmp;
    if (pgpibCmd->eos) {
        if (*pgpibCmd->eos)
            count += strlen(pgpibCmd->eos);
        else
            count++;
    }
    nread = pasynOctet->read(asynOctetPvt,pasynUser,buf,count);
    if (nread != count) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                             "Error reading waveform bytes (and trailing EOS)");
        return -1;
    }
    buf += nread;
    pgpibDpvt->msgInputLen = buf - pgpibDpvt->msg;
    asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,pgpibDpvt->msg,pgpibDpvt->msgInputLen,
        "%s readArbitraryBlockProgramData\n",pgpibDpvt->precord->name);
    return pgpibDpvt->msgInputLen;
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

static void setMsgRsp(gpibDpvt *pgpibDpvt)
{
    devGpibParmBlock *pdevGpibParmBlock = pgpibDpvt->pdevGpibParmBlock;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    portInstance *pportInstance = pdevGpibPvt->pportInstance;
    gpibCmd *pgpibCmd;
    int i,msgLenMax=0,rspLenMax=0;

    if(pdevGpibParmBlock->msgLenMax==0 && pdevGpibParmBlock->rspLenMax==0) {
        for(i=0; i<pdevGpibParmBlock->numparams; i++) {
            pgpibCmd = &pdevGpibParmBlock->gpibCmds[i];
            if(pgpibCmd->rspLen > rspLenMax) rspLenMax = pgpibCmd->rspLen;
            if(pgpibCmd->msgLen > msgLenMax) msgLenMax = pgpibCmd->msgLen;
        }
        pdevGpibParmBlock->rspLenMax = rspLenMax;
        pdevGpibParmBlock->msgLenMax = msgLenMax;
    }
    msgLenMax = pdevGpibParmBlock->msgLenMax;
    rspLenMax = pdevGpibParmBlock->rspLenMax;
    if(pportInstance->rspLenMax<rspLenMax) pportInstance->rspLenMax = rspLenMax;
    if(pportInstance->msgLenMax<msgLenMax) pportInstance->msgLenMax = msgLenMax;
}

static void registerSrqHandlerCallback(asynUser *pasynUser)
{
    portInstance *pportInstance = (portInstance *)pasynUser->userPvt;
    int status;

    status = pportInstance->pasynGpib->registerSrqHandler(
        pportInstance->asynGpibPvt,pasynUser,srqHandlerGpib,pportInstance);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s devGpib:registerSrqHandlerCallback "
            "registerSrqHandler failed %s\n",
            pportInstance->portName,pasynUser->errorMessage);
    } else {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s devGpib:registerSrqHandlerCallback\n",pportInstance->portName);
    }
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
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "devSupportGpib: link %d %s not found\n",link,asynCommonType);
        free(pportInstance);
        return 0;
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
        asynUser *pasynUser;

        pportInstance->pasynGpib = 
            (asynGpib *)pasynInterface->pinterface;
        pportInstance->asynGpibPvt = pasynInterface->drvPvt;
        /*Must not call registerSrqHandler directly. queueRequest*/
        pasynUser = pasynManager->createAsynUser(registerSrqHandlerCallback,0);
        pasynUser->userPvt = pportInstance;
        pportInstance->pasynUser = pasynUser;
        status = pasynManager->connectDevice(pasynUser,portName,0);
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s devGpibCommon:createPortInstance "
                "connectDevice failed %s\n",portName,pasynUser->errorMessage);
        }
        status = pasynManager->queueRequest(pasynUser,asynQueuePriorityHigh,0);
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s devGpibCommon:createPortInstance "
                "queueRequest failed %s\n",portName,pasynUser->errorMessage);
        }
    }
    ellAdd(&pcommonGpibPvt->portInstanceList,&pportInstance->node);
    return pportInstance;
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
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "devSupportGpib:getDeviceInstance link %d %s failed %s\n",
            link,portName,pasynUser->errorMessage);
       return -1;
    }
    pportInstance = (portInstance *)
        ellFirst(&pcommonGpibPvt->portInstanceList);
    while(pportInstance) {
        if(link==pportInstance->link) break;
        pportInstance = (portInstance *)ellNext(&pportInstance->node);
    }
    if(!pportInstance) {
        pportInstance = createPortInstance(link,pasynUser,portName);
        if(!pportInstance) return -1;
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
    return 0;
}

static void queueIt(gpibDpvt *pgpibDpvt,int isLocked)
{
    asynUser *pasynUser = pgpibDpvt->pasynUser; 
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
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s queueRequest failed timeWindow active\n",
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
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s queueRequest failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
        return;
    }
    if(!isLocked)epicsMutexUnlock(pportInstance->lock);
}

static int gpibSetEOS(gpibDpvt *pgpibDpvt, gpibCmd *pgpibCmd)
{
    asynUser *pasynUser = pgpibDpvt->pasynUser; 
    dbCommon *precord = pgpibDpvt->precord;
    asynStatus status;
    int eosLen;

    if(pgpibCmd->eos) {
        eosLen = strlen(pgpibCmd->eos);
        if(eosLen==0) eosLen = 1;
    } else {
        eosLen = 0;
    }
    status = pgpibDpvt->pasynOctet->setEos(
        pgpibDpvt->asynOctetPvt,pasynUser,pgpibCmd->eos,eosLen);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s pasynOctet->setEos failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
        return -1;
    }
    return 0;
}

static int gpibPrepareToRead(gpibDpvt *pgpibDpvt,int failure)
{
    asynUser *pasynUser = pgpibDpvt->pasynUser; 
    dbCommon *precord = pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = pgpibCmd->type;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    portInstance *pportInstance = pdevGpibPvt->pportInstance;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    int nchars = 0, lenmsg = 0;

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s gpibPrepareToRead\n",precord->name);
    if((pgpibCmd->msgLen > 0) && !pgpibDpvt->msg) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s no msg buffer\n",precord->name);
        recGblSetSevr(precord,READ_ALARM, INVALID_ALARM);
        failure = 1;
    }
    if(!failure && pdevGpibPvt->start)
        failure = pdevGpibPvt->start(pgpibDpvt,failure);
    if(failure)  goto done;
    if(cmdType&GPIBCVTIO) goto done;
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
    if (gpibSetEOS(pgpibDpvt, pgpibCmd) < 0) {
        failure = -1;
        goto done;
    }
    switch(cmdType) {
    case GPIBREADW:
    case GPIBEFASTIW:
    case GPIBREAD:
    case GPIBEFASTI:
        if(!pgpibCmd->cmd) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s pgpibCmd->cmd is null\n",precord->name);
            failure = -1; break;
        }
        lenmsg = strlen(pgpibCmd->cmd);
        nchars = writeIt(pgpibDpvt,pgpibCmd->cmd,lenmsg);
        if(nchars!=lenmsg) {
            if(cmdType&(GPIBREADW|GPIBEFASTIW)) {
                epicsTimerCancel(pdeviceInstance->srqWaitTimer);
                return gpibReadWaitComplete(pgpibDpvt,-1);
            }
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s lenmsg %d but nchars written %d\n",
                precord->name,lenmsg,nchars);
            recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
            failure = -1; break;
        }
        if(cmdType&(GPIBREADW|GPIBEFASTIW)) return 0;
    case GPIBRAWREAD:
        break;
    default:
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s gpibPrepareToRead can't handle cmdType %d",
            precord->name,cmdType);
        failure = -1;
    }
done:
    if(failure) recGblSetSevr(precord,READ_ALARM, INVALID_ALARM);
    return gpibRead(pgpibDpvt,failure);
}

static int gpibReadWaitComplete(gpibDpvt *pgpibDpvt,int failure)
{
    asynUser *pasynUser = pgpibDpvt->pasynUser; 
    dbCommon *precord = pgpibDpvt->precord;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    portInstance *pportInstance = pdevGpibPvt->pportInstance;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s gpibReadWaitComplete\n",precord->name);
    epicsMutexMustLock(pportInstance->lock);
    /*check that pgpibDpvt is owner of waitForSRQ*/
    if(!pdeviceInstance->waitForSRQ) {
        epicsMutexUnlock(pportInstance->lock);
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s gpibReadWaitComplete is not owner of waitForSRQ. Why?\n",
            precord->name);
        return -1;
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
    return gpibRead(pgpibDpvt,failure);
}

static int gpibRead(gpibDpvt *pgpibDpvt,int failure)
{
    asynUser *pasynUser = pgpibDpvt->pasynUser; 
    dbCommon *precord = pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = pgpibCmd->type;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    asynOctet *pasynOctet = pgpibDpvt->pasynOctet;
    void *asynOctetPvt = pgpibDpvt->asynOctetPvt;
    int nchars;
    asynStatus status;

    if(failure) goto done;
    if(cmdType&GPIBCVTIO) goto done;
    if(!pgpibDpvt->msg) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s pgpibDpvt->msg is null\n",precord->name);
        nchars = 0; failure = -1; goto done;
    } else {
        nchars = pasynOctet->read(asynOctetPvt,pgpibDpvt->pasynUser,
            pgpibDpvt->msg,pgpibCmd->msgLen);
    }
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s gpibRead nchars %d\n",
        precord->name,nchars);
    asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,pgpibDpvt->msg,nchars,
        "%s gpibRead\n",precord->name);
    pgpibDpvt->msgInputLen = nchars;
    if(nchars==0) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s read returned 0 characters\n",precord->name);
        gpibTimeoutHappened(pgpibDpvt);
        failure = -1; goto done;
    }
    if(nchars<pgpibCmd->msgLen) pgpibDpvt->msg[nchars] = 0;
    if(cmdType&(GPIBEFASTI|GPIBEFASTIW)) 
        pgpibDpvt->efastVal = checkEnums(pgpibDpvt->msg, pgpibCmd->P3);
done:
    if(pdevGpibPvt->finish)
        failure = pdevGpibPvt->finish(pgpibDpvt,failure);
    status = pasynManager->unlock(pgpibDpvt->pasynUser);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s pasynManager->unlock failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
    }
    if(failure) recGblSetSevr(precord,READ_ALARM, INVALID_ALARM);
    return failure;
}

static int gpibWrite(gpibDpvt *pgpibDpvt,int failure)
{
    dbCommon *precord = pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    asynGpib *pasynGpib = pgpibDpvt->pasynGpib;
    void *asynGpibPvt = pgpibDpvt->asynGpibPvt;
    int cmdType = pgpibCmd->type;
    int nchars = 0, lenMessage = 0;
    char *efasto, *msg;

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s gpibWrite\n",precord->name);
    if(!failure && pdevGpibPvt->start)
        failure = pdevGpibPvt->start(pgpibDpvt,failure);
    if(failure) {
        recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
        goto done;
    }
    if (pgpibCmd->convert) {
        int cnvrtStat;
        pasynUser->errorMessage[0] = 0;
        cnvrtStat = pgpibCmd->convert(
            pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
        if(cnvrtStat==-1) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s convert failed %s\n",precord->name,pasynUser->errorMessage);
            failure = -1;
        } else {
            lenMessage = cnvrtStat;
        }
    }
    if(failure)  goto done;
    if(cmdType&GPIBCVTIO) goto done;
    switch(cmdType) {
    case GPIBWRITE:
        if(!pgpibDpvt->msg) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s pgpibDpvt->msg is null\n",precord->name);
            recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
        } else {
            if(lenMessage==0) lenMessage = strlen(pgpibDpvt->msg);
            nchars = writeIt(pgpibDpvt,pgpibDpvt->msg,lenMessage);
        }
        break;
    case GPIBCMD:
        if(!pgpibCmd->cmd) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s pgpibCmd->cmd is null\n",precord->name);
            recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
        } else {
            if(lenMessage==0) lenMessage = strlen(pgpibCmd->cmd);
            nchars = writeIt(pgpibDpvt,pgpibCmd->cmd,lenMessage);
        }
        break;
    case GPIBACMD:
        if(!pasynGpib) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s gpibWrite got GPIBACMD but pasynGpib 0\n",precord->name);
            break;
        }
        if(!pgpibCmd->cmd) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s pgpibCmd->cmd is null\n",precord->name);
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
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s() efastVal out of range\n",precord->name);
            break;
        }
        efasto = pgpibCmd->P3[pgpibDpvt->efastVal];
        if (pgpibCmd->cmd != NULL) {
            if(pgpibDpvt->msg
            && (pgpibCmd->msgLen > (strlen(efasto)+strlen(pgpibCmd->cmd)))) {
                sprintf(pgpibDpvt->msg, "%s%s", pgpibCmd->cmd, efasto);
            } else {
                recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
                asynPrint(pasynUser,ASYN_TRACE_ERROR,
                    "%s() no msg buffer or msgLen too small\n",precord->name);
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
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s msgLen is 0\n",precord->name);
        }
        break;
    default:
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s gpibWrite cant handle cmdType %d"
            " record left with PACT true\n",precord->name,cmdType);
        goto done;
    }
    if(nchars!=lenMessage) failure = -1;
done:
    if(failure) recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
    if(pdevGpibPvt->finish)
        failure =  pdevGpibPvt->finish(pgpibDpvt,failure);
    return(failure);
}

static void queueCallback(asynUser *pasynUser)
{
    gpibDpvt *pgpibDpvt = (gpibDpvt *)pasynUser->userPvt;
    dbCommon *precord = pgpibDpvt->precord;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    portInstance *pportInstance = pdevGpibPvt->pportInstance;
    devGpibParmBlock *pdevGpibParmBlock = pgpibDpvt->pdevGpibParmBlock;
    gpibCmd *pgpibCmd;
    gpibWork work;
    int failure = 0;

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s queueCallback\n",precord->name);
    epicsMutexMustLock(pportInstance->lock);
    if(pdeviceInstance->timeoutActive)
        failure = isTimeWindowActive(pgpibDpvt) ? -1 : 0;
    if(!precord->pact) {
        epicsMutexUnlock(pportInstance->lock);
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s devSupportGpib:queueCallback but pact 0. Request ignored.\n",
            precord->name);
        return;
    }
    assert(pdevGpibPvt->work);
    work = pdevGpibPvt->work;
    pdevGpibPvt->work = 0;
    if(pportInstance->msgLen<pportInstance->msgLenMax) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            " queueCallback allocate msg length %d\n",pportInstance->msgLenMax);
        if(pportInstance->msgLen>0) free(pportInstance->msg);
        pportInstance->msg = callocMustSucceed(
            pportInstance->msgLenMax,sizeof(char),
            "devSupportGpib::queueCallback");
        pportInstance->msgLen = pportInstance->msgLenMax;
    }
    if(pportInstance->rspLen<pportInstance->rspLenMax) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            " queueCallback allocate rsp length %d\n",pportInstance->rspLenMax);
        if(pportInstance->rspLen>0) free(pportInstance->rsp);
        pportInstance->rsp = callocMustSucceed(
            pportInstance->rspLenMax,sizeof(char),
            "devSupportGpib::queueCallback");
        pportInstance->rspLen = pportInstance->rspLenMax;
    }
    pgpibCmd = &pdevGpibParmBlock->gpibCmds[pgpibDpvt->parm];
    pgpibDpvt->msg = (pgpibCmd->msgLen>0) ? pportInstance->msg : NULL;
    pgpibDpvt->rsp = (pgpibCmd->rspLen>0) ? pportInstance->rsp : NULL;
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

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s queueTimeoutCallback\n",precord->name);
    epicsMutexMustLock(pportInstance->lock);
    if(pdeviceInstance->timeoutActive) isTimeWindowActive(pgpibDpvt);
    if(!precord->pact) {
        epicsMutexUnlock(pportInstance->lock);
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s devSupportGpib:queueTimeoutCallback but pact 0. "
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
            epicsMutexUnlock(pportInstance->lock);
            epicsTimerCancel(pdeviceInstance->srqWaitTimer);
            queueIt(pdeviceInstance->pgpibDpvt,1);
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
    printf( "portName %s link %d gpibAddr %d "
       "SRQ happened but no record is attached to the gpibAddr\n",
        pportInstance->portName,pportInstance->link,gpibAddr);
}

static void srqWaitTimeoutCallback(void *parm)
{
    deviceInstance *pdeviceInstance = (deviceInstance *)parm;
    gpibDpvt *pgpibDpvt = pdeviceInstance->pgpibDpvt;
    dbCommon *precord = pgpibDpvt->precord;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    devGpibPvt *pdevGpibPvt;
    portInstance *pportInstance;

    asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s srqWaitTimeout\n", precord->name);
    pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    pportInstance = pdevGpibPvt->pportInstance;
    epicsMutexMustLock(pportInstance->lock);
    /*Check that SRQ did not occur after timeout started*/
    if(pdeviceInstance->queueRequestFromSrq) {
        epicsMutexUnlock(pportInstance->lock);
        return;
    }
    assert(pdeviceInstance->waitForSRQ);
    epicsMutexUnlock(pportInstance->lock);
    gpibReadWaitComplete(pgpibDpvt,-1);
}

static int gpibCmdIsConsistant(gpibDpvt *pgpibDpvt)
{
    asynUser *pasynUser = pgpibDpvt->pasynUser; 
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    dbCommon *precord = pgpibDpvt->precord; 

    /* If gpib spscific command make sure that pasynGpib is available*/
    if(!pgpibDpvt->pasynGpib) {
        if(pgpibCmd->type&(
        GPIBACMD|GPIBREADW|GPIBEFASTIW|GPIBIFC
        |GPIBREN|GPIBDCL|GPIBLLO|GPIBSDC|GPIBGTL|GPIBSRQHANDLER)) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s parm %d gpibCmd.type requires asynGpib but "
                "it is not implemented by port driver\n",
                 precord->name,pgpibDpvt->parm);
            return 0;
        }
    }
    if(pgpibCmd->type&GPIBSOFT && !pgpibCmd->convert) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s parm %d GPIBSOFT but convert is null\n",
            precord->name,pgpibDpvt->parm);
        return 0;
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
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s parm %d P3 must be an EFAST table\n",
                precord->name,pgpibDpvt->parm);
            return 0;
        }
    }
    if(pgpibCmd->type&(GPIBREAD|GPIBREADW)) {
        if(!pgpibCmd->cmd) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s parm %d requires cmd\n",
                precord->name,pgpibDpvt->parm);
            return 0;
        }
    }
    if(pgpibCmd->type&(GPIBCMD|GPIBACMD)) {
        if(!pgpibCmd->cmd) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s parm %d requires cmd \n",
                precord->name,pgpibDpvt->parm);
            return 0;
        }
    }
    return 1;
}

static int checkEnums(char *msg, char **enums)
{
    int i = 0;

    if(!enums) return -1;
    while (enums[i] != 0) {
        int j = 0;
        while(enums[i][j] && (enums[i][j] == msg[j]) ) j++;
        if (enums[i][j] == 0) return i;
        i++;
    }
    return -1;
}

static int writeIt(gpibDpvt *pgpibDpvt,char *message,int len)
{
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    char *rsp = pgpibDpvt->rsp;
    int rspLen = pgpibCmd->rspLen;
    dbCommon *precord = pgpibDpvt->precord;
    asynOctet *pasynOctet = pgpibDpvt->pasynOctet;
    void *asynOctetPvt = pgpibDpvt->asynOctetPvt;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    int respond2Writes = pgpibDpvt->pdevGpibParmBlock->respond2Writes;
    int nchars;

    nchars = pasynOctet->write(asynOctetPvt,pgpibDpvt->pasynUser,message,len);
    asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,message,nchars,
        "%s writeIt\n",precord->name);
    if(nchars!=len) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s write requested %d but sent %d bytes\n",
            precord->name,len,nchars);
        if(nchars==0) {
            gpibTimeoutHappened(pgpibDpvt);
        } else {
            ++pdeviceInstance->errorCount;
        }
    }
    if(respond2Writes>=0 && rspLen>0) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s respond2Writes\n",precord->name);
        if(respond2Writes>0) epicsThreadSleep((double)(respond2Writes));
        if (gpibSetEOS(pgpibDpvt, pgpibCmd) < 0) return -1;
        if (pasynOctet->read(asynOctetPvt,pgpibDpvt->pasynUser,rsp,rspLen) < 0){
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s respond2Writes read failed\n", precord->name);
            nchars = -1;
        }
    }
    return nchars;
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
    return stillActive;
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

static const iocshArg devGpibQueueTimeoutArg0 = {"portName",iocshArgString};
static const iocshArg devGpibQueueTimeoutArg1 = {"gpibAddr",iocshArgInt};
static const iocshArg devGpibQueueTimeoutArg2 = {"timeout",iocshArgDouble};
static const iocshArg *const devGpibQueueTimeoutArgs[3] =
 {&devGpibQueueTimeoutArg0,&devGpibQueueTimeoutArg1,&devGpibQueueTimeoutArg2};
static const iocshFuncDef devGpibQueueTimeoutDef =
    {"devGpibQueueTimeout", 3, devGpibQueueTimeoutArgs};
static void devGpibQueueTimeoutCall(const iocshArgBuf * args) {
    devGpibQueueTimeoutSet(args[0].sval,args[1].ival,args[2].dval);
}

static const iocshArg devGpibSrqWaitTimeoutArg0 = {"portName",iocshArgString};
static const iocshArg devGpibSrqWaitTimeoutArg1 = {"gpibAddr",iocshArgInt};
static const iocshArg devGpibSrqWaitTimeoutArg2 = {"timeout",iocshArgDouble};
static const iocshArg *const devGpibSrqWaitTimeoutArgs[3] =
 {&devGpibSrqWaitTimeoutArg0,&devGpibSrqWaitTimeoutArg1,&devGpibSrqWaitTimeoutArg2};
static const iocshFuncDef devGpibSrqWaitTimeoutDef =
    {"devGpibSrqWaitTimeout", 3, devGpibSrqWaitTimeoutArgs};
static void devGpibSrqWaitTimeoutCall(const iocshArgBuf * args) {
    devGpibSrqWaitTimeoutSet(args[0].sval,args[1].ival,args[2].dval);
}

static long init(int pass)
{
    if(pass!=0) return(0);
    iocshRegister(&devGpibQueueTimeoutDef,devGpibQueueTimeoutCall);
    iocshRegister(&devGpibSrqWaitTimeoutDef,devGpibSrqWaitTimeoutCall);
    return(0);
}

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
            printf("    gpibAddr %d\n"
                   "        timeouts %lu errors %lu\n"
                   "        queueTimeout %f srqWaitTimeout %f\n",
                pdeviceInstance->gpibAddr,
                pdeviceInstance->tmoCount,pdeviceInstance->errorCount,
                pdeviceInstance->queueTimeout,pdeviceInstance->srqWaitTimeout);
            pdeviceInstance = (deviceInstance *)ellNext(&pdeviceInstance->node);
        }
        pportInstance = (portInstance *)ellNext(&pportInstance->node);
    }
    pasynManager->freeAsynUser(pasynUser);
    return 0;
}

static gDset devGpib= {
    6,
    {report,init,0,0,0,0},
    0
};
epicsExportAddress(dset,devGpib);
