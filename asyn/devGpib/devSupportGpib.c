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
#include <iocsh.h>

#include <epicsExport.h>
#include "asynDriver.h"
#include "asynGpibDriver.h"

#include "devSupportGpib.h"

#define DEFAULT_QUEUE_TIMEOUT 60.0
#define DEFAULT_SRQ_WAIT_TIMEOUT 5.0

typedef struct commonGpibPvt {
    ELLLIST portInstanceList;
    epicsTimerQueueId timerQueue;
}commonGpibPvt;
static commonGpibPvt *pcommonGpibPvt=0;

typedef struct portInstance portInstance;

typedef enum {srqWaitIdle,srqWait,srqWaitDone,srqWaitTimedOut} srqWaitState;
typedef struct srqPvt {
    double waitTimeout;
    /*Following fields are for GPIBSRQHANDLER*/
    interruptCallbackInt32 unsollicitedHandler;
    void *unsollicitedHandlerPvt;
    /*Following fields are for GPIBREADW and GPIBEFASTIW*/
    epicsTimerId waitTimer;  /*to wait for SRQ*/
    srqWaitState waitState;
    gpibDpvt *pgpibDpvt;        /*for record waiting for SRQ*/
}srqPvt;

typedef struct deviceInstance {
    ELLNODE node; /*For portInstance.deviceInstanceList*/
    portInstance *pportInstance; 
    int gpibAddr;
    unsigned long errorCount;   /* total number of errors since boot time */
    double queueTimeout;
    srqPvt srq;
    /*Following fields are for timeWindow*/
    int timeoutActive;
    epicsTimeStamp timeoutTime;
    void    *registrarPvt; /* For pasynInt32->registerInterruptUser*/
    char    saveEos[2];
    int     saveEosLen;
}deviceInstance;

struct portInstance {
    ELLNODE node;   /*For commonGpibPvt.portInstanceList*/
    ELLLIST deviceInstanceList;
    epicsMutexId lock; 
    int link;
    char *portName;
    asynCommon *pasynCommon;
    void *asynCommonPvt;
    asynOctet *pasynOctet;
    void *asynOctetPvt;
    asynInt32 *pasynInt32;
    void      *asynInt32Pvt;
    asynGpib *pasynGpib;
    void *asynGpibPvt;
    /*The following are for shared msg buffer*/
    char *msg;   /*shared msg buffer for all gpibCmds*/
    int  msgLen;/*size of msg*/
    int  msgLenMax; /*msgLenMax all devices attached to this port*/
    char *rsp;   /*shared rsp buffer for all respond2Writes*/
    int  rspLen;/*size of rsp*/
    int  rspLenMax; /*rspLenMax all devices attached to this port*/
};

struct devGpibPvt {
    portInstance *pportInstance;
    deviceInstance *pdeviceInstance;
    gpibWork work;
    gpibStart start;
    gpibFinish finish;
};

static long initRecord(dbCommon* precord, struct link * plink);
static void processGPIBSOFT(gpibDpvt *pgpibDpvt);
static void queueReadRequest(gpibDpvt *pgpibDpvt,gpibStart start,gpibFinish finish);
static void queueWriteRequest(gpibDpvt *pgpibDpvt,gpibStart start,gpibFinish finish);
static int queueRequest(gpibDpvt *pgpibDpvt, gpibWork work);
static void registerSrqHandler(gpibDpvt *pgpibDpvt,
    interruptCallbackInt32 handler,void *unsollicitedHandlerPvt);
static int writeMsgLong(gpibDpvt *pgpibDpvt,long val);
static int writeMsgULong(gpibDpvt *pgpibDpvt,unsigned long val);
static int writeMsgDouble(gpibDpvt *pgpibDpvt,double val);
static int writeMsgString(gpibDpvt *pgpibDpvt,const char *str);
static int readArbitraryBlockProgramData(gpibDpvt *pgpibDpvt);
static int setEos(gpibDpvt *pgpibDpvt, gpibCmd *pgpibCmd);
static int restoreEos(gpibDpvt *pgpibDpvt, gpibCmd *pgpibCmd);
static void completeProcess(gpibDpvt *pgpibDpvt);

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
    readArbitraryBlockProgramData,
    setEos,
    restoreEos,
    completeProcess
};
epicsShareDef devSupportGpib *pdevSupportGpib = &gpibSupport;

/*Initialization routines*/
static void commonGpibPvtInit(void);
static void setMsgRsp(gpibDpvt *pgpibDpvt);
static portInstance *createPortInstance(
    int link,asynUser *pasynUser,const char *portName);
static int getDeviceInstance(gpibDpvt *pgpibDpvt,int link,int gpibAddr);

/*Process routines */
static int queueIt(gpibDpvt *pgpibDpvt);
static void prepareToRead(gpibDpvt *pgpibDpvt,int failure);
static void readAfterWait(gpibDpvt *pgpibDpvt,int failure);
static void gpibRead(gpibDpvt *pgpibDpvt,int failure);
static void gpibWrite(gpibDpvt *pgpibDpvt,int failure);

/*asynUser callback routines*/
static void queueCallback(asynUser *pasynUser);
static void queueTimeoutCallback(asynUser *pasynUser);

/* srq routines*/
static void srqPvtInit(asynUser *pasynUser, deviceInstance *pdeviceInstance);
static asynStatus srqReadWait(gpibDpvt *pgpibDpvt);
static void srqHandlerGpib(void *parm, asynUser *pasynUser, epicsInt32 statusByte);
static void waitTimeoutCallback(void *parm);

/*Utility routines*/
/* gpibCmdIsConsistant returns (0,1) If (is not, is) consistant*/
static int gpibCmdIsConsistant(gpibDpvt *pgpibDpvt);
static int checkEnums(char * msg, char **enums);
static void gpibErrorHappened(gpibDpvt *pgpibDpvt);
static int isTimeWindowActive(gpibDpvt *pgpibDpvt);
static int writeIt(gpibDpvt *pgpibDpvt,char *message,size_t len);

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

static void queueReadRequest(gpibDpvt *pgpibDpvt,gpibStart start,gpibFinish finish)
{
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    dbCommon *precord = pgpibDpvt->precord;

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s queueReadRequest\n",precord->name);
    pdevGpibPvt->work = prepareToRead;
    pdevGpibPvt->start = start;
    pdevGpibPvt->finish = finish;
    if(queueIt(pgpibDpvt)) return;
    recGblSetSevr(precord, SOFT_ALARM, INVALID_ALARM);
}

static void queueWriteRequest(gpibDpvt *pgpibDpvt,gpibStart start,gpibFinish finish)
{
    dbCommon *precord = pgpibDpvt->precord;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s queueWriteRequest\n",precord->name);
    pdevGpibPvt->work = gpibWrite;
    pdevGpibPvt->start = start;
    pdevGpibPvt->finish = finish;
    queueIt(pgpibDpvt);
}

static int queueRequest(gpibDpvt *pgpibDpvt, gpibWork work)
{
    dbCommon *precord = pgpibDpvt->precord;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s queueRequest\n",precord->name);
    pdevGpibPvt->work = work;
    pdevGpibPvt->start = 0;
    pdevGpibPvt->finish = 0;
    return queueIt(pgpibDpvt);
}

static void registerSrqHandler(gpibDpvt *pgpibDpvt,
    interruptCallbackInt32 handler,void *unsollicitedHandlerPvt)
{
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    dbCommon *precord = (dbCommon *)pgpibDpvt->precord;
    asynGpib *pasynGpib = pgpibDpvt->pasynGpib;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    srqPvt         *psrqPvt = &pdeviceInstance->srq;
    portInstance *pportInstance = pdevGpibPvt->pportInstance;
    int failure=0;
    
    epicsMutexMustLock(pportInstance->lock);
    if(!pasynGpib) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s asynGpib not supported\n",precord->name);
        failure = -1;
    } else if(pdeviceInstance->srq.unsollicitedHandler) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s an unsollicitedHandler already registered\n",precord->name);
        failure = -1;
    }
    if(failure==-1) {
        precord->pact = TRUE;
    }else {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s registerSrqHandler\n",precord->name);
        psrqPvt->unsollicitedHandlerPvt = unsollicitedHandlerPvt;
        psrqPvt->unsollicitedHandler = handler;
        if(psrqPvt->waitState==srqWaitIdle) {
            epicsMutexUnlock(pportInstance->lock);
            pportInstance->pasynGpib->pollAddr(
                pportInstance->asynGpibPvt,pgpibDpvt->pasynUser,1);
            return;
        }
    }
    epicsMutexUnlock(pportInstance->lock);
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
    asynStatus status;
    size_t nread;
    size_t count;
    char *endptr;
    asynOctet *pasynOctet = pgpibDpvt->pasynOctet;
    void *asynOctetPvt = pgpibDpvt->asynOctetPvt;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    char *buf = pgpibDpvt->msg;
    size_t bufSize = pgpibCmd->msgLen;
    char saveEosBuf[5];
    char *saveEos;
    int saveEosLen;
    int eomReason;

    if (pgpibCmd->eos) {
        if (*pgpibCmd->eos == '\0')
            saveEosLen = 1;
        else
            saveEosLen = (int)strlen(pgpibCmd->eos);
        saveEos = pgpibCmd->eos;
    }
    else {
        status = pasynOctet->getInputEos(asynOctetPvt,pasynUser,saveEosBuf,sizeof saveEosBuf,&saveEosLen);
        if (status != asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                      "Device EOS too long!");
            return -1;
        }
        saveEos = saveEosBuf;
    }
    if (saveEosLen)
        pasynOctet->setInputEos(asynOctetPvt,pasynUser,"#",1);

    /*
     * Read preamble (if we're using EOS to terminate messages) or the entire
     * block (if we're using some other mechanism to terminate messages).
     */
    status = pasynOctet->read(asynOctetPvt,pasynUser,buf,bufSize,&nread,&eomReason);
    if (status!=asynSuccess || nread == 0)
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                      "Error reading arbitrary block program data preamble");
    if (saveEosLen)
        pasynOctet->setInputEos(asynOctetPvt,pasynUser,saveEos,saveEosLen);
    if (status!=asynSuccess || nread == 0)
        return -1;
    buf += nread;
    if (saveEosLen) {
        if ((eomReason & ASYN_EOM_EOS) == 0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                          "Didn't find '#' to begin arbitrary block program data");
            return -1;
        }
        *buf++= '#';
        bufSize -= nread + 1;
        status = pasynOctet->read(asynOctetPvt,pasynUser,buf,1,&nread,0);
        if (status != asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                 "Error reading arbitrary block program data number of digits");
            return -1;
        }
        if ((*buf < '0') || (*buf > '9')) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "Arbitrary block program data number of digits ('\\%.2x') is not numeric",(unsigned char)*buf);
            return -1;
        }
        count = *buf - '0';
        buf += nread;
        bufSize -= nread;
        if (count == 0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                    "Arbitrary block program data number of digits is zero");
            return -1;
        }
        if (count > bufSize) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                      "Arbitrary block program data too long");
            return -1;
        }
        status = pasynOctet->read(asynOctetPvt,pasynUser,buf,count,&nread,0);
        if (status!=asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                  "Error reading arbitrary block program data number of bytes");
            return -1;
        }
        buf[nread] = '\0';
        ltmp = strtol(buf,&endptr,10);
        if ((endptr == buf) || (*endptr != '\0')) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                 "Arbitrary block program data number of bytes (%s) is not numeric",buf);
            return -1;
        }
        if ((ltmp <= 0) || (ltmp >= (long)bufSize)) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
              "Arbitrary block program data number of bytes (%lu) exceeds buffer space",ltmp);
            return -1;
        }
        buf += nread;
        bufSize -= nread;
        count = ltmp;
        pasynOctet->setInputEos(asynOctetPvt,pasynUser,NULL,0);
        while (count) {
            status = pasynOctet->read(asynOctetPvt,pasynUser,buf,count,&nread,0);
            if (status!=asynSuccess) {
                pasynOctet->setInputEos(asynOctetPvt,pasynUser,saveEos,saveEosLen);
                epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                 "Error reading arbitrary block program data");
                return -1;
            }
            count -= nread;
            buf += nread;
        }
        pasynOctet->setInputEos(asynOctetPvt,pasynUser,saveEos,saveEosLen);
        status = pasynOctet->read(asynOctetPvt,pasynUser,saveEos,1,&nread,0);
        if (status!=asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                     "Error reading EOS after arbitrary block program data");
            return -1;
        }
        if (nread != 0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
             "Unexpected characters between arbitrary block program data and EOS");
            return -1;
        }
    }
    else {
        /*
         * No EOS -- just read entire END-terminated block
         */
        if ((eomReason & ASYN_EOM_END) == 0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                          "Arbitrary block program data too long");
            return -1;
        }
    }
    pgpibDpvt->msgInputLen = (int)(buf - pgpibDpvt->msg);
    if (pgpibDpvt->msgInputLen < pgpibCmd->msgLen)
        *buf = '\0'; /* Add a hidden trailing NUL as a professional courtesy */
    asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,pgpibDpvt->msg,pgpibDpvt->msgInputLen,
        "%s readArbitraryBlockProgramData\n",pgpibDpvt->precord->name);
    return pgpibDpvt->msgInputLen;
}

static int setEos(gpibDpvt *pgpibDpvt, gpibCmd *pgpibCmd)
{
    deviceInstance *pdeviceInstance = pgpibDpvt->pdevGpibPvt->pdeviceInstance;
    asynUser    *pasynUser = pgpibDpvt->pasynUser; 
    dbCommon    *precord = pgpibDpvt->precord;
    asynOctet   *pasynOctet = pgpibDpvt->pasynOctet;
    void        *drvPvt = pgpibDpvt->asynOctetPvt;
    asynStatus  status;
    int eosLen;

    if(!pgpibCmd->eos) return 0;
    eosLen = (int)strlen(pgpibCmd->eos);
    if(eosLen==0) eosLen = 1;
    status = pasynOctet->getInputEos(drvPvt,pasynUser,
        pdeviceInstance->saveEos,sizeof(pdeviceInstance->saveEos),
        &pdeviceInstance->saveEosLen);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s pasynOctet->getInputEos failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
        return -1;
    }
    status = pasynOctet->setInputEos(drvPvt,pasynUser,pgpibCmd->eos,eosLen);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s pasynOctet->setInputEos failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
        return -1;
    }
    return 0;
}

static int restoreEos(gpibDpvt *pgpibDpvt, gpibCmd *pgpibCmd)
{
    deviceInstance *pdeviceInstance = pgpibDpvt->pdevGpibPvt->pdeviceInstance;
    asynUser    *pasynUser = pgpibDpvt->pasynUser; 
    dbCommon    *precord = pgpibDpvt->precord;
    asynOctet   *pasynOctet = pgpibDpvt->pasynOctet;
    void        *drvPvt = pgpibDpvt->asynOctetPvt;
    asynStatus  status;

    if(!pgpibCmd->eos) return 0;
    status = pasynOctet->setInputEos(drvPvt,pasynUser,
        pdeviceInstance->saveEos,pdeviceInstance->saveEosLen);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s pasynOctet->setInputEos failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
        return -1;
    }
    return 0;
}

static void completeProcess(gpibDpvt *pgpibDpvt)
{
    asynUser   *pasynUser = pgpibDpvt->pasynUser;
    dbCommon   *precord = pgpibDpvt->precord;
    asynStatus status;
    int        yesNo = 0;

    status = pasynManager->canBlock(pasynUser,&yesNo);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s pasynOctet->canBlock failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
    }
    if(yesNo) {
        callbackRequestProcessCallback(
            &pgpibDpvt->callback,precord->prio,precord);
    } else {
        precord->pact = FALSE;
    }
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
        if (pdevGpibParmBlock->respond2Writes > 4)
            printf("Warning -- %s respond2Writes is quite large (%g seconds).\n"
                   "           Perhaps this value is still being set as\n"
                   "           milliseconds rather than seconds?\n",
                    pdevGpibParmBlock->name, pdevGpibParmBlock->respond2Writes);
        for(i=0; i<pdevGpibParmBlock->numparams; i++) {
            pgpibCmd = &pdevGpibParmBlock->gpibCmds[i];
            if(pgpibCmd->rspLen > rspLenMax) rspLenMax = pgpibCmd->rspLen;
            if(pgpibCmd->msgLen > msgLenMax) msgLenMax = pgpibCmd->msgLen;
        }
        pdevGpibParmBlock->rspLenMax = rspLenMax;
        pdevGpibParmBlock->msgLenMax = msgLenMax;
        if(pdevGpibParmBlock->rspLenMax > 0) {
            if(pdevGpibParmBlock->respond2Writes<0) {
                printf("Warning -- %s has rspLen>0 but respond2Writes is not set.\n", pdevGpibParmBlock->name);
            }
        }
        else {
            if(pdevGpibParmBlock->respond2Writes>=0) {
                printf("Warning -- %s respond2Writes is set but has no command table entry with rspLen>0.\n", pdevGpibParmBlock->name);
            }
        }
        if(pdevGpibParmBlock->msgLenMax == 0) {
            printf("Warning -- %s has no command table entry with msgLen>0.\n", pdevGpibParmBlock->name);
        }

    }
    msgLenMax = pdevGpibParmBlock->msgLenMax;
    rspLenMax = pdevGpibParmBlock->rspLenMax;
    if(pportInstance->rspLenMax<rspLenMax) pportInstance->rspLenMax = rspLenMax;
    if(pportInstance->msgLenMax<msgLenMax) pportInstance->msgLenMax = msgLenMax;
}

static portInstance *createPortInstance(
    int link,asynUser *pasynUser,const char *portName)
{
    portInstance *pportInstance;
    asynInterface *pasynInterface;
    int           portNameSize;

    portNameSize = (int)strlen(portName) + 1;
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
    pasynInterface = pasynManager->findInterface(pasynUser,asynInt32Type,1);
    if(pasynInterface) {
        pportInstance->pasynInt32 = 
            (asynInt32 *)pasynInterface->pinterface;
        pportInstance->asynInt32Pvt = pasynInterface->drvPvt;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynGpibType,1);
    if(pasynInterface) {
        pportInstance->pasynGpib = 
            (asynGpib *)pasynInterface->pinterface;
        pportInstance->asynGpibPvt = pasynInterface->drvPvt;
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
        pdeviceInstance->pportInstance = pportInstance;
        pdeviceInstance->gpibAddr = gpibAddr;
        pdeviceInstance->queueTimeout = DEFAULT_QUEUE_TIMEOUT;
        srqPvtInit(pasynUser,pdeviceInstance);
        if(pportInstance->pasynInt32) {
            pasynUser->reason = ASYN_REASON_SIGNAL;
            status = pportInstance->pasynInt32->registerInterruptUser(
                pportInstance->asynInt32Pvt,pasynUser,
                srqHandlerGpib,pdeviceInstance,&pdeviceInstance->registrarPvt);
            if(status!=asynSuccess) {
                asynPrint(pasynUser,ASYN_TRACE_ERROR,
                    "%s devGpib registerSrqHandler failed %s\n",
                    pportInstance->portName,pasynUser->errorMessage);
            } else {
                asynPrint(pasynUser,ASYN_TRACE_FLOW,
                    "%s devGpib:registerSrqHandler\n",pportInstance->portName);
            }
        }
        ellAdd(&pportInstance->deviceInstanceList,&pdeviceInstance->node);
    }
    pdevGpibPvt->pportInstance = pportInstance;
    pdevGpibPvt->pdeviceInstance = pdeviceInstance;
    return 0;
}

static int queueIt(gpibDpvt *pgpibDpvt)
{
    asynUser *pasynUser = pgpibDpvt->pasynUser; 
    dbCommon *precord = pgpibDpvt->precord;
    devGpibParmBlock *pdevGpibParmBlock = pgpibDpvt->pdevGpibParmBlock;
    gpibCmd *pgpibCmd = &pdevGpibParmBlock->gpibCmds[pgpibDpvt->parm];
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    portInstance *pportInstance = pdevGpibPvt->pportInstance;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    asynStatus status;
    asynQueuePriority priority = pgpibCmd->pri;

    epicsMutexMustLock(pportInstance->lock);
    if(pdeviceInstance->timeoutActive) {
        if(isTimeWindowActive(pgpibDpvt)) {
            recGblSetSevr(precord, SOFT_ALARM, INVALID_ALARM);
            epicsMutexUnlock(pportInstance->lock);
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s queueRequest failed timeWindow active\n",
                precord->name);
            return 0;
        }
    }
    status = pasynManager->queueRequest(pgpibDpvt->pasynUser,
        priority,pdeviceInstance->queueTimeout);
    if(status!=asynSuccess) {
        precord->pact = FALSE;
        recGblSetSevr(precord, SOFT_ALARM, INVALID_ALARM);
        epicsMutexUnlock(pportInstance->lock);
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s queueRequest failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
        return 0;
    }
    precord->pact = TRUE;
    epicsMutexUnlock(pportInstance->lock);
    return 1;
}

static void prepareToRead(gpibDpvt *pgpibDpvt,int failure)
{
    asynUser *pasynUser = pgpibDpvt->pasynUser; 
    dbCommon *precord = pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = pgpibCmd->type;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    asynOctet *pasynOctet = pgpibDpvt->pasynOctet;
    void *asynOctetPvt = pgpibDpvt->asynOctetPvt;
    int nchars = 0, lenmsg = 0;
    asynStatus status;

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s prepareToRead\n",precord->name);
    if(!failure && pdevGpibPvt->start)
        failure = pdevGpibPvt->start(pgpibDpvt,failure);
    if(failure)  goto done;
    if(cmdType&GPIBCVTIO) goto done;
    if (setEos(pgpibDpvt, pgpibCmd) < 0) {
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
        status = pasynOctet->flush(asynOctetPvt,pasynUser);
        if(status != asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s flush error\n",
                precord->name);
            recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
            failure = -1; break;
        }
        lenmsg = (int)strlen(pgpibCmd->cmd);
        nchars = writeIt(pgpibDpvt,pgpibCmd->cmd,lenmsg);
        if(nchars!=lenmsg) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s lenmsg %d but nchars written %d\n",
                precord->name,lenmsg,nchars);
            recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
            failure = -1; break;
        }
        if(cmdType&(GPIBREADW|GPIBEFASTIW)) {
            status = srqReadWait(pgpibDpvt);
            if(status==asynSuccess) return; /*readAfterWait will complete*/
            failure = -1; break;
        }
        break;
    case GPIBRAWREAD:
        break;
    default:
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s prepareToRead can't handle cmdType %d\n",
            precord->name,cmdType);
        failure = -1;
    }
done:
    if(failure) recGblSetSevr(precord,READ_ALARM, INVALID_ALARM);
    gpibRead(pgpibDpvt,failure);
}

static void readAfterWait(gpibDpvt *pgpibDpvt,int failure)
{
    asynUser       *pasynUser = pgpibDpvt->pasynUser;
    dbCommon       *precord = pgpibDpvt->precord;
    devGpibPvt     *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    portInstance   *pportInstance = pdevGpibPvt->pportInstance;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    srqPvt         *psrqPvt = &pdeviceInstance->srq;
    asynStatus     status;

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s readAfterWait\n",precord->name);
    epicsMutexMustLock(pportInstance->lock);
    assert(psrqPvt->pgpibDpvt==pgpibDpvt);
    if(!psrqPvt->unsollicitedHandler) {
        pportInstance->pasynGpib->pollAddr(
            pportInstance->asynGpibPvt,pgpibDpvt->pasynUser,0);
    }
    if(psrqPvt->waitState==srqWaitTimedOut) failure = -1;
    psrqPvt->waitState = srqWaitIdle;
    psrqPvt->pgpibDpvt = 0;
    epicsMutexUnlock(pportInstance->lock);
    status = pasynManager->unblockProcessCallback(pgpibDpvt->pasynUser,0);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s pasynManager->unlockDevice failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
    }
    gpibRead(pgpibDpvt,failure);
}

static void gpibRead(gpibDpvt *pgpibDpvt,int failure)
{
    asynUser *pasynUser = pgpibDpvt->pasynUser; 
    dbCommon *precord = pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = pgpibCmd->type;
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    asynOctet *pasynOctet = pgpibDpvt->pasynOctet;
    void *asynOctetPvt = pgpibDpvt->asynOctetPvt;
    size_t nchars = 0;

    if(failure) goto done;
    if(cmdType&GPIBCVTIO) goto done;
    if(!pgpibDpvt->msg) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s pgpibDpvt->msg is null\n",precord->name);
        nchars = 0; failure = -1; goto done;
    } else {
        pasynOctet->read(asynOctetPvt,pasynUser,
            pgpibDpvt->msg,pgpibCmd->msgLen,&nchars,0);
    }
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s gpibRead nchars %lu\n",
        precord->name,(unsigned long)nchars);
    if(nchars > 0) {
        asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,pgpibDpvt->msg,nchars,
            "%s gpibRead\n",precord->name);
    } else {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s read status \"%s\" nin %lu\n",
            precord->name, pasynUser->errorMessage,(unsigned long)nchars);
        pgpibDpvt->msgInputLen = 0;
        gpibErrorHappened(pgpibDpvt);
        failure = -1; goto done;
    }
    pgpibDpvt->msgInputLen = (int)nchars;
    if((int)nchars<pgpibCmd->msgLen) pgpibDpvt->msg[nchars] = 0;
    if(cmdType&(GPIBEFASTI|GPIBEFASTIW)) 
        pgpibDpvt->efastVal = checkEnums(pgpibDpvt->msg, pgpibCmd->P3);
done:
    restoreEos(pgpibDpvt,pgpibCmd);
    if(pdevGpibPvt->finish) pdevGpibPvt->finish(pgpibDpvt,failure);
}

static void gpibWrite(gpibDpvt *pgpibDpvt,int failure)
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
            if(lenMessage==0) lenMessage = (int)strlen(pgpibDpvt->msg);
            nchars = writeIt(pgpibDpvt,pgpibDpvt->msg,lenMessage);
        }
        break;
    case GPIBCMD:
        if(!pgpibCmd->cmd) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s pgpibCmd->cmd is null\n",precord->name);
            recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
        } else {
            if(lenMessage==0) lenMessage = (int)strlen(pgpibCmd->cmd);
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
            if(lenMessage==0) lenMessage = (int)strlen(pgpibCmd->cmd);
            nchars = pasynGpib->addressedCmd(
                asynGpibPvt,pgpibDpvt->pasynUser,
                pgpibCmd->cmd,lenMessage);
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
            && (pgpibCmd->msgLen > (int)(strlen(efasto)+strlen(pgpibCmd->cmd)))) {
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
        lenMessage = msg ? (int)strlen(msg) : 0;
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
    if(pdevGpibPvt->finish) pdevGpibPvt->finish(pgpibDpvt,failure);
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

static void srqPvtInit(asynUser *pasynUser, deviceInstance *pdeviceInstance)
{
    srqPvt *psrqPvt = &pdeviceInstance->srq;

    psrqPvt->waitTimeout = DEFAULT_SRQ_WAIT_TIMEOUT;
    psrqPvt->waitTimer = epicsTimerQueueCreateTimer(
            pcommonGpibPvt->timerQueue,waitTimeoutCallback,pdeviceInstance);
}

static asynStatus srqReadWait(gpibDpvt *pgpibDpvt)
{
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    dbCommon *precord = pgpibDpvt->precord;
    portInstance *pportInstance = pdevGpibPvt->pportInstance;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    srqPvt *psrqPvt = &pdeviceInstance->srq;
    asynStatus status = asynSuccess;

    epicsMutexMustLock(pportInstance->lock);
    psrqPvt->waitState = srqWait;
    status = pasynManager->blockProcessCallback(pgpibDpvt->pasynUser,0);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s pasynManager->lockDevice failed %s\n",
            precord->name,pgpibDpvt->pasynUser->errorMessage);
        recGblSetSevr(precord, SOFT_ALARM, INVALID_ALARM);
    } else {
        if(!psrqPvt->unsollicitedHandler) {
            pportInstance->pasynGpib->pollAddr(
                pportInstance->asynGpibPvt,pgpibDpvt->pasynUser,1);
        }
        pdevGpibPvt->work = readAfterWait;
        psrqPvt->pgpibDpvt = pgpibDpvt;
        epicsTimerStartDelay(psrqPvt->waitTimer,
            psrqPvt->waitTimeout);
    }
    epicsMutexUnlock(pportInstance->lock);
    return status;
}

static void srqHandlerGpib(void *parm, asynUser *pasynUser, epicsInt32 statusByte)
{
    deviceInstance *pdeviceInstance = (deviceInstance *)parm;
    srqPvt *psrqPvt = &pdeviceInstance->srq;
    portInstance *pportInstance = pdeviceInstance->pportInstance;

    epicsMutexMustLock(pportInstance->lock);
    switch(psrqPvt->waitState) {
    case srqWait:
        psrqPvt->waitState = srqWaitDone;
        epicsMutexUnlock(pportInstance->lock);
        epicsTimerCancel(psrqPvt->waitTimer);
        queueIt(psrqPvt->pgpibDpvt);
        return;
    case srqWaitDone: 
        epicsMutexUnlock(pportInstance->lock);
        printf( "portName %s link %d gpibAddr %d "
           "Extra SRQ before readAfterWait\n",
            pportInstance->portName,pportInstance->link,
            pdeviceInstance->gpibAddr);
        return;
    case srqWaitTimedOut: /*waitTimeoutCallback handled it*/
        epicsMutexUnlock(pportInstance->lock);
        return;
    case srqWaitIdle:
        if(psrqPvt->unsollicitedHandler) {
            epicsMutexUnlock(pportInstance->lock);
            psrqPvt->unsollicitedHandler(
                psrqPvt->unsollicitedHandlerPvt,pasynUser,statusByte);
            return;
        }
        break;
    }
    epicsMutexUnlock(pportInstance->lock);
    printf( "portName %s link %d gpibAddr %d "
       "SRQ happened but no record is attached to the gpibAddr\n",
        pportInstance->portName,pportInstance->link,pdeviceInstance->gpibAddr);
}

static void waitTimeoutCallback(void *parm)
{
    deviceInstance *pdeviceInstance = (deviceInstance *)parm;
    srqPvt *psrqPvt = &pdeviceInstance->srq;
    portInstance *pportInstance = pdeviceInstance->pportInstance;
    gpibDpvt *pgpibDpvt;
    dbCommon *precord;
    asynUser *pasynUser;

    epicsMutexMustLock(pportInstance->lock);
    if(psrqPvt->waitState!=srqWait) {
        epicsMutexUnlock(pportInstance->lock);
        printf("waitTimeoutCallback but waitState!=srqWait\n");
        return;
    }
    pgpibDpvt = psrqPvt->pgpibDpvt;
    assert(pgpibDpvt);
    precord = pgpibDpvt->precord;
    pasynUser = pgpibDpvt->pasynUser;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s waitTimeout\n", precord->name);
    psrqPvt->waitState = srqWaitTimedOut;
    epicsMutexUnlock(pportInstance->lock);
    queueIt(psrqPvt->pgpibDpvt);
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

static int writeIt(gpibDpvt *pgpibDpvt,char *message,size_t len)
{
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    char *rsp = pgpibDpvt->rsp;
    int rspLen = pgpibCmd->rspLen;
    dbCommon *precord = pgpibDpvt->precord;
    asynOctet *pasynOctet = pgpibDpvt->pasynOctet;
    void *asynOctetPvt = pgpibDpvt->asynOctetPvt;
    int respond2Writes = (int)pgpibDpvt->pdevGpibParmBlock->respond2Writes;
    asynStatus status;
    size_t nchars;

    status = pasynOctet->write(asynOctetPvt,pasynUser, message,len,&nchars);
    if(nchars==len) {
        asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,message,nchars,
                                            "%s writeIt\n",precord->name);
    } else {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s write status \"%s\" requested %lu but sent %lu bytes\n",
                precord->name,pasynUser->errorMessage,(unsigned long)len,(unsigned long)nchars);
            gpibErrorHappened(pgpibDpvt);
    }
    if(respond2Writes>=0 && rspLen>0) {
        size_t nrsp;
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s respond2Writes\n",precord->name);
        if(respond2Writes>0) epicsThreadSleep(respond2Writes);
        if (setEos(pgpibDpvt, pgpibCmd) < 0) return -1;
        status = pasynOctet->read(asynOctetPvt,pasynUser,rsp,rspLen,&nrsp,0);
        if (status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s respond2Writes read failed\n", precord->name);
        }
        restoreEos(pgpibDpvt,pgpibCmd);
    }
    return (int)nchars;
}

static void gpibErrorHappened(gpibDpvt *pgpibDpvt)
{
    devGpibPvt *pdevGpibPvt = pgpibDpvt->pdevGpibPvt;
    deviceInstance *pdeviceInstance = pdevGpibPvt->pdeviceInstance;
    portInstance *pportInstance = pdevGpibPvt->pportInstance;

    epicsMutexMustLock(pportInstance->lock);
    pdeviceInstance->timeoutActive = TRUE;
    epicsTimeGetCurrent(&pdeviceInstance->timeoutTime);
    ++pdeviceInstance->errorCount;
    epicsMutexUnlock(pportInstance->lock);
    asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR, "%s error.\n",
                                                    pgpibDpvt->precord->name);
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

    pdeviceInstance->srq.waitTimeout = timeout;
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
                   "        errors %lu\n"
                   "        queueTimeout %f waitTimeout %f\n",
                pdeviceInstance->gpibAddr,
                pdeviceInstance->errorCount,
                pdeviceInstance->queueTimeout,pdeviceInstance->srq.waitTimeout);
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
