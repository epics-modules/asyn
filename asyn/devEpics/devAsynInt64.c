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
#include <int64inRecord.h>
#include <int64outRecord.h>
#include <recSup.h>
#include <devSup.h>

#include <epicsExport.h>
#include "asynDriver.h"
#include "asynDrvUser.h"
#include "asynInt64.h"
#include "asynInt64SyncIO.h"
#include "asynEpicsUtils.h"

#define INIT_OK 0
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
    int               asyncProcessingActive;
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
static void processCallbackInput(asynUser *pasynUser);
static void processCallbackOutput(asynUser *pasynUser);
static void outputCallbackCallback(CALLBACK *pcb);
static int  getCallbackValue(devPvt *pPvt);
static void interruptCallbackInput(void *drvPvt, asynUser *pasynUser,
                epicsInt64 value);
static void interruptCallbackOutput(void *drvPvt, asynUser *pasynUser,
                epicsInt64 value);

static long initLLi(int64inRecord *pli);
static long initLLo(int64outRecord *plo);
static long processLLi(int64inRecord *pr);
static long processLLo(int64outRecord *pr);

typedef struct analogDset { /* analog  dset */
    long          number;
    DEVSUPFUN     dev_report;
    DEVSUPFUN     init;
    DEVSUPFUN     init_record;
    DEVSUPFUN     get_ioint_info;
    DEVSUPFUN     processCommon;/*(0)=>(success ) */
    DEVSUPFUN     special_linconv;
} analogDset;

analogDset asynInt64In = {
    5,0,0,initLLi,       getIoIntInfo, processLLi };
analogDset asynInt64Out = {
    5,0,0,initLLo,       getIoIntInfo, processLLo };

epicsExportAddress(dset, asynInt64In);
epicsExportAddress(dset, asynInt64Out);

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
        const char *callbackString;
        DBENTRY *pdbentry = dbAllocEntry(pdbbase);
        status = dbFindRecord(pdbentry, pr->name);
        if (status) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s %s::%s error finding record\n",
                pr->name, driverName, functionName);
            goto bad;
        }
        callbackString = dbGetInfo(pdbentry, "asyn:READBACK");
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
    asynStatus status;
    const char *sizeString;
    static const char *functionName="createRingBuffer";
    
    if (!pPvt->ringBuffer) {
        DBENTRY *pdbentry = dbAllocEntry(pdbbase);
        pPvt->ringSize = DEFAULT_RING_BUFFER_SIZE;
        status = dbFindRecord(pdbentry, pr->name);
        if (status) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s %s::%s error finding record\n",
                pr->name, driverName, functionName);
            return -1;
        }
        sizeString = dbGetInfo(pdbentry, "asyn:FIFO");
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
            pr->udf = 0;
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
