/* devAsynInt64.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
    Authors:  Mark Rivers and Marty Kraimer
    05-Sept-2004
*/

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <alarm.h>
#include <recGbl.h>
#include "epicsMath.h"
#include <dbAccess.h>
#include <dbDefs.h>
#include <dbEvent.h>
#include <dbStaticLib.h>
#include <link.h>
#include <cantProceed.h>
#include <epicsPrint.h>
#include <epicsMutex.h>
#include <epicsThread.h>
#include <cantProceed.h>
#include <dbCommon.h>
#include <dbScan.h>
#include <callback.h>
#ifdef HAVE_DEVINT64
#include <int64inRecord.h>
#include <int64outRecord.h>
#endif
#include <longinRecord.h>
#include <longoutRecord.h>
#include <aiRecord.h>
#include <aoRecord.h>
#include <recSup.h>
#include <devSup.h>
#include <cvtTable.h>
#include <menuConvert.h>

#include <epicsExport.h>
#include "asynDriver.h"
#include "asynDrvUser.h"
#include "asynInt64.h"
#include "asynInt64SyncIO.h"
#include "asynEpicsUtils.h"
#include "devEpicsPvt.h"

#define INIT_OK 0
#define INIT_DO_NOT_CONVERT 2
#define INIT_ERROR -1

#define DEFAULT_RING_BUFFER_SIZE 10

static const char *driverName = "devAsynInt64";

typedef struct ringBufferElement {
    epicsInt64          value;
    epicsTimeStamp      time;
    asynStatus          status;
    epicsAlarmCondition alarmStatus;
    epicsAlarmSeverity  alarmSeverity;
} ringBufferElement;

typedef struct devPvt{
    dbCommon          *pr;
    asynUser          *pasynUser;
    asynUser          *pasynUserSync;
    asynInt64         *pint64;
    void              *int64Pvt;
    void              *registrarPvt;
    int               canBlock;
    epicsInt64        deviceLow;
    epicsInt64        deviceHigh;
    epicsMutexId      devPvtLock;
    ringBufferElement *ringBuffer;
    int               ringHead;
    int               ringTail;
    int               ringSize;
    int               ringBufferOverflows;
    ringBufferElement result;
    asynStatus        lastStatus;
    interruptCallbackInt64 interruptCallback;
    double            sum;
    int               numAverage;
    int               isAiAverage;
    int               isIOIntrScan;
    int               asyncProcessingActive;
    int               bipolar;
    epicsInt32        mask;
    epicsInt32        signBit;
    CALLBACK          processCallback;
    CALLBACK          outputCallback;
    int               newOutputCallbackValue;
    int               numDeferredOutputCallbacks;
    IOSCANPVT         ioScanPvt;
    char              *portName;
    char              *userParam;
    int               addr;
    asynStatus        previousQueueRequestStatus;
}devPvt;

static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt);
static long createRingBuffer(dbCommon *pr);
static long convertAi(aiRecord *pai, int pass);
static long convertAo(aoRecord *pao, int pass);
static void processCallbackInput(asynUser *pasynUser);
static void processCallbackOutput(asynUser *pasynUser);
static void outputCallbackCallback(CALLBACK *pcb);
static int  getCallbackValue(devPvt *pPvt);
static void interruptCallbackInput(void *drvPvt, asynUser *pasynUser,
                epicsInt64 value);
static void interruptCallbackOutput(void *drvPvt, asynUser *pasynUser,
                epicsInt64 value);
static void interruptCallbackAverage(void *drvPvt, asynUser *pasynUser,
                epicsInt64 value);

typedef struct analogDset { /* analog  dset */
    long          number;
    DEVSUPFUN     dev_report;
    DEVSUPFUN     init;
    DEVSUPFUN     init_record;
    DEVSUPFUN     get_ioint_info;
    DEVSUPFUN     processCommon;/*(0)=>(success ) */
    DEVSUPFUN     special_linconv;
} analogDset;

#ifdef HAVE_DEVINT64
static long initLLi(int64inRecord *pli);
static long initLLo(int64outRecord *plo);
static long processLLi(int64inRecord *pr);
static long processLLo(int64outRecord *pr);

analogDset asynInt64In = {
    5,0,0,initLLi,       getIoIntInfo, processLLi };
analogDset asynInt64Out = {
    5,0,0,initLLo,       getIoIntInfo, processLLo };
epicsExportAddress(dset, asynInt64In);
epicsExportAddress(dset, asynInt64Out);
#endif

static long initAi(aiRecord *pai);
static long initAiAverage(aiRecord *pai);
static long initAo(aoRecord *pao);
static long initLi(longinRecord *pli);
static long initLo(longoutRecord *plo);

static long processAi(aiRecord *pr);
static long processAiAverage(aiRecord *pr);
static long processAo(aoRecord *pr);
static long processLi(longinRecord *pr);
static long processLo(longoutRecord *pr);


analogDset asynAiInt64 = {
    6,0,0,initAi,       getIoIntInfo, processAi, convertAi };
analogDset asynAiInt64Average = {
    6,0,0,initAiAverage,getIoIntInfo, processAiAverage , convertAi };
analogDset asynAoInt64 = {
    6,0,0,initAo,       getIoIntInfo, processAo , convertAo };
analogDset asynLiInt64 = {
    5,0,0,initLi,       getIoIntInfo, processLi };
analogDset asynLoInt64 = {
    5,0,0,initLo,       getIoIntInfo, processLo };
epicsExportAddress(dset, asynAiInt64);
epicsExportAddress(dset, asynAiInt64Average);
epicsExportAddress(dset, asynAoInt64);
epicsExportAddress(dset, asynLiInt64);
epicsExportAddress(dset, asynLoInt64);

static long initCommon(dbCommon *pr, DBLINK *plink,
    userCallback processCallback,interruptCallbackInt64 interruptCallback)
{
    devPvt *pPvt;
    asynStatus status;
    asynUser *pasynUser;
    asynInterface *pasynInterface;
    static const char *functionName="initCommon";

    pPvt = callocMustSucceed(1, sizeof(*pPvt), "devAsynInt64::initCommon");
    pr->dpvt = pPvt;
    pPvt->pr = pr;
    /* Create asynUser */
    pasynUser = pasynManager->createAsynUser(processCallback, 0);
    pasynUser->userPvt = pPvt;
    pPvt->pasynUser = pasynUser;
    pPvt->devPvtLock = epicsMutexCreate();

    /* Parse the link to get addr and port */
    status = pasynEpicsUtils->parseLink(pasynUser, plink,
                &pPvt->portName, &pPvt->addr, &pPvt->userParam);
    if (status != asynSuccess) {
        printf("%s %s::%s  %s\n",
                     pr->name, driverName, functionName, pasynUser->errorMessage);
        goto bad;
    }

    /* Connect to device */
    status = pasynManager->connectDevice(pasynUser, pPvt->portName, pPvt->addr);
    if (status != asynSuccess) {
        printf("%s %s::%s connectDevice failed %s\n",
                     pr->name, driverName, functionName, pasynUser->errorMessage);
        goto bad;
    }
    status = pasynManager->canBlock(pPvt->pasynUser, &pPvt->canBlock);
    if (status != asynSuccess) {
        printf("%s %s::%s canBlock failed %s\n",
                     pr->name, driverName, functionName, pasynUser->errorMessage);
        goto bad;
    }
    /*call drvUserCreate*/
    pasynInterface = pasynManager->findInterface(pasynUser,asynDrvUserType,1);
    if(pasynInterface && pPvt->userParam) {
        asynDrvUser *pasynDrvUser;
        void       *drvPvt;

        pasynDrvUser = (asynDrvUser *)pasynInterface->pinterface;
        drvPvt = pasynInterface->drvPvt;
        status = pasynDrvUser->create(drvPvt,pasynUser,pPvt->userParam,0,0);
        if(status!=asynSuccess) {
            printf("%s %s::%s drvUserCreate %s\n",
                     pr->name, driverName, functionName, pasynUser->errorMessage);
            goto bad;
        }
    }
    /* Get interface asynInt64 */
    pasynInterface = pasynManager->findInterface(pasynUser, asynInt64Type, 1);
    if (!pasynInterface) {
        printf("%s %s::%s findInterface asynInt64Type %s\n",
                     pr->name, driverName, functionName,pasynUser->errorMessage);
        goto bad;
    }
    pPvt->pint64 = pasynInterface->pinterface;
    pPvt->int64Pvt = pasynInterface->drvPvt;
    scanIoInit(&pPvt->ioScanPvt);
    pPvt->interruptCallback = interruptCallback;
    /* Initialize synchronous interface */
    status = pasynInt64SyncIO->connect(pPvt->portName, pPvt->addr,
                 &pPvt->pasynUserSync, pPvt->userParam);
    if (status != asynSuccess) {
        printf("%s %s::%s Int64SyncIO->connect failed %s\n",
               pr->name, driverName, functionName, pPvt->pasynUserSync->errorMessage);
        goto bad;
    }
    /* If the info field "asyn:READBACK" is 1 and interruptCallback is not NULL
     * then register for callbacks on output records */
    if (interruptCallback) {
        int enableCallbacks=0;
        const char *callbackString = asynDbGetInfo(pr, "asyn:READBACK");
        if (callbackString) enableCallbacks = atoi(callbackString);
        if (enableCallbacks) {
            status = createRingBuffer(pr);
            if (status!=asynSuccess) goto bad;
            status = pPvt->pint64->registerInterruptUser(
               pPvt->int64Pvt,pPvt->pasynUser,
               pPvt->interruptCallback,pPvt,&pPvt->registrarPvt);
            if (status!=asynSuccess) {
                printf("%s %s::%s error calling registerInterruptUser %s\n",
                       pr->name, driverName, functionName,pPvt->pasynUser->errorMessage);
            }
            /* Initialize the interrupt callback */
            callbackSetCallback(outputCallbackCallback, &pPvt->outputCallback);
            callbackSetPriority(pr->prio, &pPvt->outputCallback);
            callbackSetUser(pPvt, &pPvt->outputCallback);
        }
    }
    return INIT_OK;
bad:
    recGblSetSevr(pr,LINK_ALARM,INVALID_ALARM);
    pr->pact=1;
    return INIT_ERROR;
}

static long createRingBuffer(dbCommon *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    const char *sizeString;

    if (!pPvt->ringBuffer) {
        pPvt->ringSize = DEFAULT_RING_BUFFER_SIZE;
        sizeString = asynDbGetInfo(pr, "asyn:FIFO");
        if (sizeString) pPvt->ringSize = atoi(sizeString);
        pPvt->ringBuffer = callocMustSucceed(pPvt->ringSize+1, sizeof *pPvt->ringBuffer, "devAsynInt64::createRingBuffer");
    }
    return asynSuccess;
}

static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    asynStatus status;
    static const char *functionName="getIoIntInfo";

    /* If initCommon failed then pPvt->pint64 is NULL, return error */
    if (!pPvt->pint64) return -1;

    if (cmd == 0) {
        /* Add to scan list.  Register interrupts */
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s %s::%s registering interrupt\n",
            pr->name, driverName, functionName);
        status = createRingBuffer(pr);
        status = pPvt->pint64->registerInterruptUser(
           pPvt->int64Pvt,pPvt->pasynUser,
           pPvt->interruptCallback,pPvt,&pPvt->registrarPvt);
        if(status!=asynSuccess) {
            printf("%s %s::%s registerInterruptUser %s\n",
                   pr->name, driverName, functionName,pPvt->pasynUser->errorMessage);
        }
    } else {
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s %s::%s canceling interrupt\n",
             pr->name, driverName, functionName);
        status = pPvt->pint64->cancelInterruptUser(pPvt->int64Pvt,
             pPvt->pasynUser,pPvt->registrarPvt);
        if(status!=asynSuccess) {
            printf("%s %s::%s cancelInterruptUser %s\n",
                   pr->name, driverName, functionName,pPvt->pasynUser->errorMessage);
        }
    }
    *iopvt = pPvt->ioScanPvt;
    return 0;
}

static long convertAi(aiRecord *precord, int pass)
{
    devPvt *pPvt = (devPvt *)precord->dpvt;
    double eguf,egul,deviceHigh,deviceLow;

    if (pass==0) return 0;
    /* set linear conversion slope */
    if(pPvt->deviceHigh!=pPvt->deviceLow) {
        eguf = precord->eguf;
        egul = precord->egul;
        deviceHigh = (double)pPvt->deviceHigh;
        deviceLow = (double)pPvt->deviceLow;
        precord->eslo = (eguf - egul)/(deviceHigh - deviceLow);
        precord->eoff = (deviceHigh*egul - deviceLow*eguf)/
                        (deviceHigh - deviceLow);
    }
    return 0;
}

static long convertAo(aoRecord *precord, int pass)
{
    devPvt *pPvt = (devPvt *)precord->dpvt;
    double eguf,egul,deviceHigh,deviceLow;

    if (pass==0) return 0;
    /* set linear conversion slope */
    if(pPvt->deviceHigh!=pPvt->deviceLow) {
        eguf = precord->eguf;
        egul = precord->egul;
        deviceHigh = (double)pPvt->deviceHigh;
        deviceLow = (double)pPvt->deviceLow;
        precord->eslo = (eguf - egul)/(deviceHigh - deviceLow);
        precord->eoff = (deviceHigh*egul - deviceLow*eguf)/
                        (deviceHigh - deviceLow);
    }
    return 0;
}

static void processCallbackInput(asynUser *pasynUser)
{
    devPvt *pPvt = (devPvt *)pasynUser->userPvt;
    dbCommon *pr = (dbCommon *)pPvt->pr;
    static const char *functionName="processCallbackInput";

    pPvt->result.status = pPvt->pint64->read(pPvt->int64Pvt, pPvt->pasynUser, &pPvt->result.value);
    pPvt->result.time = pPvt->pasynUser->timestamp;
    pPvt->result.alarmStatus = pPvt->pasynUser->alarmStatus;
    pPvt->result.alarmSeverity = pPvt->pasynUser->alarmSeverity;
    if (pPvt->result.status == asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
            "%s %s::%s process value=%lld\n",pr->name, driverName, functionName,pPvt->result.value);
    } else {
        if (pPvt->result.status != pPvt->lastStatus) {
            asynPrint(pasynUser, ASYN_TRACE_ERROR,
                 "%s %s::%s process read error %s\n",
                 pr->name, driverName, functionName, pasynUser->errorMessage);
        }
    }
    pPvt->lastStatus = pPvt->result.status;
    if(pr->pact) callbackRequestProcessCallback(&pPvt->processCallback,pr->prio,pr);
}

static void processCallbackOutput(asynUser *pasynUser)
{
    devPvt *pPvt = (devPvt *)pasynUser->userPvt;
    dbCommon *pr = pPvt->pr;
    static const char *functionName="processCallbackOutput";

    pPvt->result.status = pPvt->pint64->write(pPvt->int64Pvt, pPvt->pasynUser,pPvt->result.value);
    pPvt->result.time = pPvt->pasynUser->timestamp;
    pPvt->result.alarmStatus = pPvt->pasynUser->alarmStatus;
    pPvt->result.alarmSeverity = pPvt->pasynUser->alarmSeverity;
    if(pPvt->result.status == asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
            "%s %s::%s process value %lld\n",pr->name, driverName, functionName,pPvt->result.value);
    } else {
        if (pPvt->result.status != pPvt->lastStatus) {
            asynPrint(pasynUser, ASYN_TRACE_ERROR,
                "%s %s::%s process error %s\n",
                pr->name, driverName, functionName, pasynUser->errorMessage);
        }
    }
    pPvt->lastStatus = pPvt->result.status;
    if(pr->pact) callbackRequestProcessCallback(&pPvt->processCallback,pr->prio,pr);
}

static void interruptCallbackInput(void *drvPvt, asynUser *pasynUser,
                epicsInt64 value)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    dbCommon *pr = pPvt->pr;
    ringBufferElement *rp;
    static const char *functionName="interruptCallbackInput";

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s %s::%s new value=%lld\n",
        pr->name, driverName, functionName, value);
    /* There is a problem.  A driver could be calling us with a value after
     * this record has registered for callbacks but before EPICS has set interruptAccept,
     * which means that scanIoRequest will return immediately.
     * This is very bad, because if we have pushed a value into the ring buffer
     * it won't get popped off because the record won't process.  The values
     * read the next time the record processes would then be stale.
     * We previously worked around this problem by waiting here for interruptAccept.
     * But that does not work if the callback is coming from the thread that is executing
     * iocInit, which can happen with synchronous drivers (ASYN_CANBLOCK=0) that do callbacks
     * when a value is written to them, which can happen in initRecord for an output record.
     * Instead we just return.  There will then be nothing in the ring buffer, so the first
     * read will do a read from the driver, which should be OK. */
    if (!interruptAccept) return;
    epicsMutexLock(pPvt->devPvtLock);
    rp = &pPvt->ringBuffer[pPvt->ringHead];
    rp->value = value;
    rp->time = pasynUser->timestamp;
    rp->status = pasynUser->auxStatus;
    rp->alarmStatus = pasynUser->alarmStatus;
    rp->alarmSeverity = pasynUser->alarmSeverity;
    pPvt->ringHead = (pPvt->ringHead==pPvt->ringSize) ? 0 : pPvt->ringHead+1;
    if (pPvt->ringHead == pPvt->ringTail) {
        /* There was no room in the ring buffer.  In the past we just threw away
         * the new value.  However, it is better to remove the oldest value from the
         * ring buffer and add the new one.  That way the final value the record receives
         * is guaranteed to be the most recent value */
        pPvt->ringTail = (pPvt->ringTail==pPvt->ringSize) ? 0 : pPvt->ringTail+1;
        pPvt->ringBufferOverflows++;
    } else {
        /* We only need to request the record to process if we added a new
         * element to the ring buffer, not if we just replaced an element. */
        scanIoRequest(pPvt->ioScanPvt);
    }
    epicsMutexUnlock(pPvt->devPvtLock);
}

static void interruptCallbackOutput(void *drvPvt, asynUser *pasynUser,
                epicsInt64 value)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    dbCommon *pr = pPvt->pr;
    ringBufferElement *rp;
    static const char *functionName="interruptCallbackOutput";

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s %s::%s new value=%lld\n",
        pr->name, driverName, functionName, value);
    if (!interruptAccept) return;
    epicsMutexLock(pPvt->devPvtLock);
    rp = &pPvt->ringBuffer[pPvt->ringHead];
    rp->value = value;
    rp->time = pasynUser->timestamp;
    rp->status = pasynUser->auxStatus;
    rp->alarmStatus = pasynUser->alarmStatus;
    rp->alarmSeverity = pasynUser->alarmSeverity;
    pPvt->ringHead = (pPvt->ringHead==pPvt->ringSize) ? 0 : pPvt->ringHead+1;
    if (pPvt->ringHead == pPvt->ringTail) {
        pPvt->ringTail = (pPvt->ringTail==pPvt->ringSize) ? 0 : pPvt->ringTail+1;
        pPvt->ringBufferOverflows++;
    } else {
        /* If this callback was received during asynchronous record processing
         * we must defer calling callbackRequest until end of record processing */
        if (pPvt->asyncProcessingActive) {
            pPvt->numDeferredOutputCallbacks++;
        } else {
            callbackRequest(&pPvt->outputCallback);
        }
    }
    epicsMutexUnlock(pPvt->devPvtLock);
}

static void interruptCallbackAverage(void *drvPvt, asynUser *pasynUser,
                epicsInt64 value)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    aiRecord *pai = (aiRecord *)pPvt->pr;
    ringBufferElement *rp;
    int numToAverage;
    static const char *functionName="interruptCallbackAverage";

    if (pPvt->mask) {
        value &= pPvt->mask;
        if (pPvt->bipolar && (value & pPvt->signBit)) value |= ~pPvt->mask;
    }
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s %s::%s new value=%lld\n",
         pai->name, driverName, functionName, value);
    if (!interruptAccept) return;
    epicsMutexLock(pPvt->devPvtLock);
    pPvt->numAverage++;
    pPvt->sum += (double)value;
    /* We use the SVAL field to hold the number of values to average when SCAN=I/O Intr
     * We should be calling dbScanLock when accessing pPvt->isIOIntrScan and pai->sval but that leads to deadlocks
     * because we have the asynPortDriver lock in the driver, and we would be taking the scan lock
     * after the asynPortDriver lock, which is the opposite of normal record processing.
     * pPvt->isIOIntrScan is an int, so should be OK to read because write to it is atomic?
     * pai->sval is a double so it may not be completely safe to read without the lock? */
    if ((pPvt->isIOIntrScan)) {
        numToAverage = (int)(pai->sval + 0.5);
        if (numToAverage < 1) numToAverage = 1;
        if (pPvt->numAverage >= numToAverage) {
            double dval;
            rp = &pPvt->ringBuffer[pPvt->ringHead];
            dval = pPvt->sum/pPvt->numAverage;
            dval += (pPvt->sum>0.0) ? 0.5 : -0.5;
            rp->value = (epicsInt32)dval;
            pPvt->numAverage = 0;
            pPvt->sum = 0.;
            rp->time = pasynUser->timestamp;
            rp->status = pasynUser->auxStatus;
            rp->alarmStatus = pasynUser->alarmStatus;
            rp->alarmSeverity = pasynUser->alarmSeverity;
            pPvt->ringHead = (pPvt->ringHead==pPvt->ringSize) ? 0 : pPvt->ringHead+1;
            if (pPvt->ringHead == pPvt->ringTail) {
                /* There was no room in the ring buffer.  In the past we just threw away
                 * the new value.  However, it is better to remove the oldest value from the
                 * ring buffer and add the new one.  That way the final value the record receives
                 * is guaranteed to be the most recent value */
                pPvt->ringTail = (pPvt->ringTail==pPvt->ringSize) ? 0 : pPvt->ringTail+1;
                pPvt->ringBufferOverflows++;
            } /* End ring buffer full */
            else {
                /* We only need to request the record to process if we added a new
                 * element to the ring buffer, not if we just replaced an element. */
                scanIoRequest(pPvt->ioScanPvt);
            }
        } /* End numAverage=SVAL, so time to compute average */
    } /* End SCAN=I/O Intr */
    else {
        pPvt->result.status |= pasynUser->auxStatus;
        pPvt->result.alarmStatus = pasynUser->alarmStatus;
        pPvt->result.alarmSeverity = pasynUser->alarmSeverity;
    }
    epicsMutexUnlock(pPvt->devPvtLock);
}

static void outputCallbackCallback(CALLBACK *pcb)
{
    static const char *functionName="outputCallbackCallback";

    devPvt *pPvt;
    callbackGetUser(pPvt, pcb);
    {
        dbCommon *pr = pPvt->pr;
        dbScanLock(pr);
        epicsMutexLock(pPvt->devPvtLock);
        pPvt->newOutputCallbackValue = 1;
        /* We need to set udf=0 here so that it is already cleared when dbProcess is called */
        pr->udf = 0;
        dbProcess(pr);
        if (pPvt->newOutputCallbackValue != 0) {
            /* We called dbProcess but the record did not process, perhaps because PACT was 1
             * Need to remove ring buffer element */
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s %s::%s warning dbProcess did not process record, PACT=%d\n",
                pr->name, driverName, functionName,pr->pact);
            getCallbackValue(pPvt);
            pPvt->newOutputCallbackValue = 0;
        }
        epicsMutexUnlock(pPvt->devPvtLock);
        dbScanUnlock(pr);
    }
}

static int getCallbackValue(devPvt *pPvt)
{
    int ret = 0;
    static const char *functionName="getCallbackValue";

    epicsMutexLock(pPvt->devPvtLock);
    if (pPvt->ringTail != pPvt->ringHead) {
        if (pPvt->ringBufferOverflows > 0) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_WARNING,
                "%s %s::%s warning, %d ring buffer overflows\n",
                pPvt->pr->name, driverName, functionName, pPvt->ringBufferOverflows);
            pPvt->ringBufferOverflows = 0;
        }
        pPvt->result = pPvt->ringBuffer[pPvt->ringTail];
        pPvt->ringTail = (pPvt->ringTail==pPvt->ringSize) ? 0 : pPvt->ringTail+1;
        asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
            "%s %s::%s from ringBuffer value=%lld\n",
            pPvt->pr->name, driverName, functionName,pPvt->result.value);
        ret = 1;
    }
    epicsMutexUnlock(pPvt->devPvtLock);
    return ret;
}

static void reportQueueRequestStatus(devPvt *pPvt, asynStatus status)
{
    static const char *functionName="reportQueueRequestStatus";

    if (status != asynSuccess) pPvt->result.status = status;
    if (pPvt->previousQueueRequestStatus != status) {
        pPvt->previousQueueRequestStatus = status;
        if (status == asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s %s::%s queueRequest status returned to normal\n",
                pPvt->pr->name, driverName, functionName);
        } else {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s %s::%s queueRequest error %s\n",
                pPvt->pr->name, driverName, functionName,pPvt->pasynUser->errorMessage);
        }
    }
}

#ifdef HAVE_DEVINT64
static long initLLi(int64inRecord *pr)
{
    int status;

    status = initCommon((dbCommon *)pr,&pr->inp,
       processCallbackInput,interruptCallbackInput);

    return status;
}

static long processLLi(int64inRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    int status;

    if(!getCallbackValue(pPvt) && !pr->pact) {
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        reportQueueRequestStatus(pPvt, status);
    }
    pr->time = pPvt->result.time;
    pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status,
                                            READ_ALARM, &pPvt->result.alarmStatus,
                                            INVALID_ALARM, &pPvt->result.alarmSeverity);
    (void)recGblSetSevr(pr, pPvt->result.alarmStatus, pPvt->result.alarmSeverity);
    if(pPvt->result.status==asynSuccess) {
        pr->val = pPvt->result.value;
        pr->udf=0;
        return 0;
    }
    else {
        pPvt->result.status = asynSuccess;
        return -1;
    }
}

static long initLLo(int64outRecord *pr)
{
    devPvt *pPvt;
    int status;
    epicsInt64 value;

    status = initCommon((dbCommon *)pr,&pr->out,
        processCallbackOutput,interruptCallbackOutput);
    if (status != INIT_OK) return status;
    pPvt = pr->dpvt;
    /* Read the current value from the device */
    status = pasynInt64SyncIO->read(pPvt->pasynUserSync,
                      &value, pPvt->pasynUser->timeout);
    if (status == asynSuccess) {
        pr->val = value;
        pr->udf = 0;
    }
    return INIT_OK;
}

static long processLLo(int64outRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    int status;

    epicsMutexLock(pPvt->devPvtLock);
    if (pPvt->newOutputCallbackValue && getCallbackValue(pPvt)) {
        /* We got a callback from the driver */
        if (pPvt->result.status == asynSuccess) {
            pr->val = pPvt->result.value;
        }
    } else if(pr->pact == 0) {
        pPvt->result.value = pr->val;
        if(pPvt->canBlock) {
            pr->pact = 1;
            pPvt->asyncProcessingActive = 1;
        }
        epicsMutexUnlock(pPvt->devPvtLock);
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        epicsMutexLock(pPvt->devPvtLock);
        reportQueueRequestStatus(pPvt, status);
    }
    pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status,
                                            WRITE_ALARM, &pPvt->result.alarmStatus,
                                            INVALID_ALARM, &pPvt->result.alarmSeverity);
    (void)recGblSetSevr(pr, pPvt->result.alarmStatus, pPvt->result.alarmSeverity);
    if (pPvt->numDeferredOutputCallbacks > 0) {
        callbackRequest(&pPvt->outputCallback);
        pPvt->numDeferredOutputCallbacks--;
    }
    pPvt->newOutputCallbackValue = 0;
    pPvt->asyncProcessingActive = 0;
    epicsMutexUnlock(pPvt->devPvtLock);
    if(pPvt->result.status == asynSuccess) {
        return 0;
    }
    else {
        pPvt->result.status = asynSuccess;
        return -1;
    }
}
#endif  /* HAVE_DEVINT64 */

static long initAi(aiRecord *pr)
{
    devPvt *pPvt;
    int status;

    status = initCommon((dbCommon *)pr,&pr->inp,
        processCallbackInput,interruptCallbackInput);
    if(status != INIT_OK) return status;
    pPvt = pr->dpvt;
    /* Don't call getBounds if we already have non-zero values from
     * parseLinkMask */
    if ((pPvt->deviceLow == 0) && (pPvt->deviceHigh == 0)) {
        pasynInt64SyncIO->getBounds(pPvt->pasynUserSync,
                                    &pPvt->deviceLow, &pPvt->deviceHigh);
    }
    convertAi(pr, 1);
    return INIT_OK;
}
static long processAi(aiRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    int status;

    if(!getCallbackValue(pPvt) && !pr->pact) {
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        reportQueueRequestStatus(pPvt, status);
    }
    pr->time = pPvt->result.time;
    pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status,
                                            READ_ALARM, &pPvt->result.alarmStatus,
                                            INVALID_ALARM, &pPvt->result.alarmSeverity);
    (void)recGblSetSevr(pr, pPvt->result.alarmStatus, pPvt->result.alarmSeverity);
    if (pPvt->result.status == asynSuccess) {
        pr->val = (epicsFloat64)pPvt->result.value;
        pr->udf = 0;
        return 2;
    }
    else {
        return -1;
    }
}

static long initAiAverage(aiRecord *pr)
{
    devPvt *pPvt;
    int status;
    static const char *functionName="initAiAverage";

    status = initCommon((dbCommon *)pr, &pr->inp,
        NULL, interruptCallbackAverage);
    if (status != INIT_OK) return status;
    pPvt = pr->dpvt;
    pPvt->isAiAverage = 1;
    status = pPvt->pint64->registerInterruptUser(
                 pPvt->int64Pvt,pPvt->pasynUser,
                 interruptCallbackAverage,pPvt,&pPvt->registrarPvt);
    if(status!=asynSuccess) {
        printf("%s %s::%s registerInterruptUser %s\n",
               pr->name, driverName, functionName,pPvt->pasynUser->errorMessage);
    }
    /* Don't call getBounds if we already have non-zero values from
     * parseLinkMask */
    if ((pPvt->deviceLow == 0) && (pPvt->deviceHigh == 0)) {
        pasynInt64SyncIO->getBounds(pPvt->pasynUserSync,
                                &pPvt->deviceLow, &pPvt->deviceHigh);
    }
    convertAi(pr, 1);
    return INIT_OK;
}

static long processAiAverage(aiRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    double val;
    static const char *functionName="processAiAverage";

    epicsMutexLock(pPvt->devPvtLock);

    if (getCallbackValue(pPvt)) {
        /* Record is I/O Intr scanned and the average has been put in the ring buffer */
        val = (double)pPvt->result.value;
        pr->time = pPvt->result.time;
    } else {
        if (pPvt->numAverage == 0) {
            (void)recGblSetSevr(pr, UDF_ALARM, INVALID_ALARM);
            pr->udf = 1;
            epicsMutexUnlock(pPvt->devPvtLock);
            return -2;
        }
        val = pPvt->sum/pPvt->numAverage;
        pPvt->numAverage = 0;
        pPvt->sum = 0.;
    }
    epicsMutexUnlock(pPvt->devPvtLock);
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s %s::%s val=%f, status=%d\n",pr->name, driverName, functionName, val, pPvt->result.status);
    pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status,
                                            READ_ALARM, &pPvt->result.alarmStatus,
                                            INVALID_ALARM, &pPvt->result.alarmSeverity);
    (void)recGblSetSevr(pr, pPvt->result.alarmStatus, pPvt->result.alarmSeverity);
    if (pPvt->result.status == asynSuccess) {
        pr->val = val;
        pr->udf = 0;
        return 2;
    }
    else {
        pPvt->result.status = asynSuccess;
        return -1;
    }
}

static long initAo(aoRecord *pao)
{
    devPvt *pPvt;
    int status;
    epicsInt64 value;

    status = initCommon((dbCommon *)pao,&pao->out,
        processCallbackOutput,interruptCallbackOutput);
    if (status != INIT_OK) return status;
    pPvt = pao->dpvt;
    /* Don't call getBounds if we already have non-zero values from
     * parseLinkMask */
    if ((pPvt->deviceLow == 0) && (pPvt->deviceHigh == 0)) {
        pasynInt64SyncIO->getBounds(pPvt->pasynUserSync,
                                &pPvt->deviceLow, &pPvt->deviceHigh);
    }
    convertAo(pao, 1);
    /* Read the current value from the device */
    status = pasynInt64SyncIO->read(pPvt->pasynUserSync,
                      &value, pPvt->pasynUser->timeout);
    if (pPvt->mask) {
        value &= pPvt->mask;
        if (pPvt->bipolar && (value & pPvt->signBit)) value |= ~pPvt->mask;
    }
    if (status == asynSuccess) {
        pao->val = (epicsFloat64)value;
        return INIT_DO_NOT_CONVERT;
    }
    return INIT_DO_NOT_CONVERT; /* Do not convert */
}

static long processAo(aoRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    asynStatus status;
    double     value;
    /* static const char *functionName="processAo"; */

    epicsMutexLock(pPvt->devPvtLock);
    if (pPvt->newOutputCallbackValue && getCallbackValue(pPvt)) {
        /* We got a callback from the driver */
        if (pPvt->result.status == asynSuccess) {
            value = (double)pPvt->result.value;
            if (pr->linr == menuConvertNO_CONVERSION){
                ; /*do nothing*/
            } else if ((pr->linr == menuConvertLINEAR) ||
                       (pr->linr == menuConvertSLOPE)) {
                value = value*pr->eslo + pr->eoff;
            }
            pr->val = value;
            pr->udf = isnan(value);
        }
    } else if(pr->pact == 0) {
        pPvt->result.value = (epicsInt64)pr->val;
        if(pPvt->canBlock) {
            pr->pact = 1;
            pPvt->asyncProcessingActive = 1;
        }
        epicsMutexUnlock(pPvt->devPvtLock);
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        epicsMutexLock(pPvt->devPvtLock);
        if(pPvt->canBlock) pr->pact = 0;
        reportQueueRequestStatus(pPvt, status);
    }
    pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status,
                                            WRITE_ALARM, &pPvt->result.alarmStatus,
                                            INVALID_ALARM, &pPvt->result.alarmSeverity);
    (void)recGblSetSevr(pr, pPvt->result.alarmStatus, pPvt->result.alarmSeverity);
    if (pPvt->numDeferredOutputCallbacks > 0) {
        callbackRequest(&pPvt->outputCallback);
        pPvt->numDeferredOutputCallbacks--;
    }
    pPvt->newOutputCallbackValue = 0;
    pPvt->asyncProcessingActive = 0;
    epicsMutexUnlock(pPvt->devPvtLock);
    if(pPvt->result.status == asynSuccess) {
        return 0;
    }
    else {
        pPvt->result.status = asynSuccess;
        return -1;
    }
}

static long initLi(longinRecord *pr)
{
    int status;

    status = initCommon((dbCommon *)pr,&pr->inp,
       processCallbackInput,interruptCallbackInput);

    return status;
}

static long processLi(longinRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    int status;

    if(!getCallbackValue(pPvt) && !pr->pact) {
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        reportQueueRequestStatus(pPvt, status);
    }
    pr->time = pPvt->result.time;
    pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status,
                                            READ_ALARM, &pPvt->result.alarmStatus,
                                            INVALID_ALARM, &pPvt->result.alarmSeverity);
    (void)recGblSetSevr(pr, pPvt->result.alarmStatus, pPvt->result.alarmSeverity);
    if(pPvt->result.status==asynSuccess) {
        pr->val = (epicsInt32)pPvt->result.value;
        pr->udf=0;
        return 0;
    }
    else {
        pPvt->result.status = asynSuccess;
        return -1;
    }
}

static long initLo(longoutRecord *pr)
{
    devPvt *pPvt;
    int status;
    epicsInt64 value;

    status = initCommon((dbCommon *)pr,&pr->out,
        processCallbackOutput,interruptCallbackOutput);
    if (status != INIT_OK) return status;
    pPvt = pr->dpvt;
    /* Read the current value from the device */
    status = pasynInt64SyncIO->read(pPvt->pasynUserSync,
                      &value, pPvt->pasynUser->timeout);
    if (status == asynSuccess) {
        pr->val = (epicsInt32)value;
        pr->udf = 0;
    }
    return INIT_OK;
}

static long processLo(longoutRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    int status;

    epicsMutexLock(pPvt->devPvtLock);
    if (pPvt->newOutputCallbackValue && getCallbackValue(pPvt)) {
        /* We got a callback from the driver */
        if (pPvt->result.status == asynSuccess) {
            pr->val = (epicsInt32)pPvt->result.value;
        }
    } else if(pr->pact == 0) {
        pPvt->result.value = pr->val;
        if(pPvt->canBlock) {
            pr->pact = 1;
            pPvt->asyncProcessingActive = 1;
        }
        epicsMutexUnlock(pPvt->devPvtLock);
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        epicsMutexLock(pPvt->devPvtLock);
        reportQueueRequestStatus(pPvt, status);
    }
    pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status,
                                            WRITE_ALARM, &pPvt->result.alarmStatus,
                                            INVALID_ALARM, &pPvt->result.alarmSeverity);
    (void)recGblSetSevr(pr, pPvt->result.alarmStatus, pPvt->result.alarmSeverity);
    if (pPvt->numDeferredOutputCallbacks > 0) {
        callbackRequest(&pPvt->outputCallback);
        pPvt->numDeferredOutputCallbacks--;
    }
    pPvt->newOutputCallbackValue = 0;
    pPvt->asyncProcessingActive = 0;
    epicsMutexUnlock(pPvt->devPvtLock);
    if(pPvt->result.status == asynSuccess) {
        return 0;
    }
    else {
        pPvt->result.status = asynSuccess;
        return -1;
    }
}
