/* devAsynFloat64.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
    Author:  Mark Rivers and Marty Kraimer
    14-OCT-2004

*/

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include <alarm.h>
#include <callback.h>
#include <recGbl.h>
#include <dbAccess.h>
#include <dbDefs.h>
#include <dbStaticLib.h>
#include <link.h>
#include <epicsPrint.h>
#include <epicsMutex.h>
#include <epicsThread.h>
#include <epicsMath.h>
#include <cantProceed.h>
#include <dbCommon.h>
#include <dbScan.h>
#include <aoRecord.h>
#include <aiRecord.h>
#include <recSup.h>
#include <devSup.h>

#include <epicsExport.h>
#include "asynDriver.h"
#include "asynDrvUser.h"
#include "asynFloat64SyncIO.h"
#include "asynEpicsUtils.h"
#include "asynFloat64.h"

#define INIT_OK 0
#define INIT_DO_NOT_CONVERT 2
#define INIT_ERROR -1

#define DEFAULT_RING_BUFFER_SIZE 10

static const char *driverName = "devAsynFloat64";

typedef struct ringBufferElement {
    epicsFloat64        value;
    epicsTimeStamp      time;
    asynStatus          status;
    epicsAlarmCondition alarmStatus;
    epicsAlarmSeverity  alarmSeverity;
} ringBufferElement;

typedef struct devPvt{
    dbCommon          *pr;
    asynUser          *pasynUser;
    asynUser          *pasynUserSync;
    asynFloat64       *pfloat64;
    void              *float64Pvt;
    void              *registrarPvt;
    int               canBlock;
    epicsMutexId      devPvtLock;
    ringBufferElement *ringBuffer;
    int               ringHead;
    int               ringTail;
    int               ringSize;
    int               ringBufferOverflows;
    ringBufferElement result;
    asynStatus        lastStatus;
    epicsFloat64      sum;
    interruptCallbackFloat64 interruptCallback;
    int               numAverage;
    int               isAiAverage;
    int               isIOIntrScan;
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

static long initCommon(dbCommon *pr, DBLINK *plink,
    userCallback processCallback,interruptCallbackFloat64 interruptCallback);
static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt);
static long createRingBuffer(dbCommon *pr);
static void processCallbackInput(asynUser *pasynUser);
static void processCallbackOutput(asynUser *pasynUser);
static void outputCallbackCallback(CALLBACK *pcb);
static int  getCallbackValue(devPvt *pPvt);
static void interruptCallbackInput(void *drvPvt, asynUser *pasynUser,
                epicsFloat64 value);
static void interruptCallbackOutput(void *drvPvt, asynUser *pasynUser,
                epicsFloat64 value);
static void interruptCallbackAverage(void *drvPvt, asynUser *pasynUser,
                epicsFloat64 value);

static long initAi(aiRecord *pai);
static long initAo(aoRecord *pai);
static long initAiAverage(aiRecord *pai);
static long processAi(aiRecord *pai);
static long processAo(aoRecord *pai);
static long processAiAverage(aiRecord *pai);

typedef struct analogDset { /* analog  dset */
    long          number;
    DEVSUPFUN     dev_report;
    DEVSUPFUN     init;
    DEVSUPFUN     init_record;
    DEVSUPFUN     get_ioint_info;
    DEVSUPFUN     processCommon;/*(0)=>(success ) */
    DEVSUPFUN     special_linconv;
} analogDset;

analogDset asynAiFloat64 = {
    6, 0, 0, initAi,        getIoIntInfo, processAi, 0};
analogDset asynAoFloat64 = {
    6, 0, 0, initAo,        getIoIntInfo, processAo, 0};
analogDset asynAiFloat64Average = {
    6, 0, 0, initAiAverage, getIoIntInfo, processAiAverage, 0};

epicsExportAddress(dset, asynAiFloat64);
epicsExportAddress(dset, asynAoFloat64);
epicsExportAddress(dset, asynAiFloat64Average);

static long initCommon(dbCommon *pr, DBLINK *plink,
    userCallback processCallback,interruptCallbackFloat64 interruptCallback)
{
    devPvt *pPvt;
    asynStatus status;
    asynUser *pasynUser;
    asynInterface *pasynInterface;
    static const char *functionName="initCommon";

    pPvt = callocMustSucceed(1, sizeof(*pPvt), "%s::%s");
    pr->dpvt = pPvt;
    pPvt->pr = pr;
    /* Create asynUser */
    pasynUser = pasynManager->createAsynUser(processCallback, 0);
    pasynUser->userPvt = pPvt;
    pPvt->pasynUser = pasynUser;
    pPvt->devPvtLock = epicsMutexCreate();
    /* Parse the link to get addr and port */
    status = pasynEpicsUtils->parseLink(pasynUser, plink,
                &pPvt->portName, &pPvt->addr,&pPvt->userParam);
    if (status != asynSuccess) {
        printf("%s %s::%s %s\n",
                     pr->name, driverName, functionName,pasynUser->errorMessage);
        goto bad;
    }
    /* Connect to device */
    status = pasynManager->connectDevice(pasynUser, pPvt->portName, pPvt->addr);
    if (status != asynSuccess) {
        printf("%s %s::%s connectDevice failed %s\n",
                     pr->name, driverName, functionName,pasynUser->errorMessage);
        goto bad;
    }
    status = pasynManager->canBlock(pPvt->pasynUser, &pPvt->canBlock);
    if (status != asynSuccess) {
        printf("%s %s::%s canBlock failed %s\n",
                     pr->name, driverName, functionName,pasynUser->errorMessage);
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
                     pr->name, driverName, functionName,pasynUser->errorMessage);
            goto bad;
        }
    }
    /* Get interface asynFloat64 */
    pasynInterface = pasynManager->findInterface(pasynUser, asynFloat64Type, 1);
    if (!pasynInterface) {
        printf("%s %s::%s findInterface asynFloat64Type %s\n",
                     pr->name, driverName, functionName,pasynUser->errorMessage);
        goto bad;
    }
    pPvt->pfloat64 = pasynInterface->pinterface;
    pPvt->float64Pvt = pasynInterface->drvPvt;

    /* Initialize synchronous interface */
    status = pasynFloat64SyncIO->connect(pPvt->portName, pPvt->addr,
                 &pPvt->pasynUserSync, pPvt->userParam);
    if (status != asynSuccess) {
        printf("%s %s::%s Float64SyncIO->connect failed %s\n",
               pr->name, driverName, functionName,pPvt->pasynUserSync->errorMessage);
        goto bad;
    }

    scanIoInit(&pPvt->ioScanPvt);
    pPvt->interruptCallback = interruptCallback;

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
            status = pPvt->pfloat64->registerInterruptUser(
               pPvt->float64Pvt,pPvt->pasynUser,
               pPvt->interruptCallback,pPvt,&pPvt->registrarPvt);
            if(status!=asynSuccess) {
                printf("%s %s::initRecord error calling registerInterruptUser %s\n",
                       pr->name, driverName,pPvt->pasynUser->errorMessage);
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
        pPvt->ringBuffer = callocMustSucceed(pPvt->ringSize+1, sizeof *pPvt->ringBuffer, "%s::createRingBuffer");
    }
    return asynSuccess;
}


static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    asynStatus status;
    static const char *functionName="getIoIntInfo";

    /* If initCommon failed then pPvt->pfloat64 is NULL, return error */
    if (!pPvt->pfloat64) return -1;

    if (cmd == 0) {
        /* Add to scan list.  Register interrupts, create ring buffer */
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s %s::%s registering interrupt\n",
            pr->name, driverName, functionName);
        createRingBuffer(pr);
        /* Set a flag indicating that we are in I/O Intr scan mode. Used in aiAverage mode. */
         pPvt->isIOIntrScan = 1;
        /* For aiAverage we don't enable callbacks here, because they are always enabled in any scan mode. */
        if (!pPvt->isAiAverage) {
            status = pPvt->pfloat64->registerInterruptUser(
               pPvt->float64Pvt,pPvt->pasynUser,
               pPvt->interruptCallback,pPvt,&pPvt->registrarPvt);
            if(status!=asynSuccess) {
                printf("%s %s::%s registerInterruptUser %s\n",
                       pr->name, driverName, functionName,pPvt->pasynUser->errorMessage);
            }
        }
    } else {
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s %s::%s cancelling interrupt\n",
             pr->name, driverName, functionName);
        /* Set a flag indicating that we are not in I/O Intr scan mode. Used in aiAverage mode. */
        pPvt->isIOIntrScan = 0;
        /* For aiAverage we don't disable callbacks here, because they are always enabled in any scan mode. */
        if (!pPvt->isAiAverage) {
            status = pPvt->pfloat64->cancelInterruptUser(pPvt->float64Pvt,
                 pPvt->pasynUser,pPvt->registrarPvt);
            if(status!=asynSuccess) {
                printf("%s %s::%s cancelInterruptUser %s\n",
                       pr->name, driverName, functionName,pPvt->pasynUser->errorMessage);
            }
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

    pPvt->result.status = pPvt->pfloat64->read(pPvt->float64Pvt, pPvt->pasynUser, &pPvt->result.value);
    pPvt->result.time = pPvt->pasynUser->timestamp;
    pPvt->result.alarmStatus = pPvt->pasynUser->alarmStatus;
    pPvt->result.alarmSeverity = pPvt->pasynUser->alarmSeverity;
    if (pPvt->result.status == asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
            "%s %s::%s process value=%f\n", pr->name, driverName, functionName,
            pPvt->result.value);
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

    pPvt->result.status = pPvt->pfloat64->write(pPvt->float64Pvt, pPvt->pasynUser,pPvt->result.value);
    pPvt->result.time = pPvt->pasynUser->timestamp;
    pPvt->result.alarmStatus = pPvt->pasynUser->alarmStatus;
    pPvt->result.alarmSeverity = pPvt->pasynUser->alarmSeverity;
    if(pPvt->result.status == asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
            "%s %s::%s process value %f\n", pr->name, driverName, functionName, pPvt->result.value);
    } else {
        if (pPvt->result.status != pPvt->lastStatus) {
            asynPrint(pasynUser, ASYN_TRACE_ERROR,
                "%s %s::%s process write error %s\n",
                pr->name, driverName, functionName, pasynUser->errorMessage);
        }
    }
    pPvt->lastStatus = pPvt->result.status;
    pPvt->lastStatus = pPvt->result.status;
    if(pr->pact) callbackRequestProcessCallback(&pPvt->processCallback,pr->prio,pr);
}

static void interruptCallbackInput(void *drvPvt, asynUser *pasynUser,
                epicsFloat64 value)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    dbCommon *pr = pPvt->pr;
    ringBufferElement *rp;
    static const char *functionName="interruptCallbackInput";

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s %s::%s new value=%f\n",
        pr->name, driverName, functionName,value);
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
                epicsFloat64 value)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    dbCommon *pr = pPvt->pr;
    ringBufferElement *rp;
    static const char *functionName="interruptCallbackOutput";

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s %s::%s new value=%f\n",
        pr->name, driverName, functionName,value);
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
    devPvt *pPvt;
    static const char *functionName="outputCallbackCallback";

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

static void interruptCallbackAverage(void *drvPvt, asynUser *pasynUser,
                epicsFloat64 value)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    dbCommon *pr = pPvt->pr;
    aiRecord *pai = (aiRecord *)pr;
    ringBufferElement *rp;
    int numToAverage;
    static const char *functionName="interruptCallbackAverage";

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s %s::%s new value=%f\n",
        pr->name, driverName, functionName,value);
    if (!interruptAccept) return;
    epicsMutexLock(pPvt->devPvtLock);
    pPvt->numAverage++;
    pPvt->sum += value;
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
            rp = &pPvt->ringBuffer[pPvt->ringHead];
            rp->value = pPvt->sum/pPvt->numAverage;
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

static int getCallbackValue(devPvt *pPvt)
{
    int ret = 0;
    static const char *functionName="getCallbackValue";

    epicsMutexLock(pPvt->devPvtLock);
    if (pPvt->ringTail != pPvt->ringHead) {
        if (pPvt->ringBufferOverflows > 0) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_WARNING,
                "%s %s::%s warning, %d ring buffer overflows\n",
                                    pPvt->pr->name, driverName, functionName,pPvt->ringBufferOverflows);
            pPvt->ringBufferOverflows = 0;
        }
        pPvt->result = pPvt->ringBuffer[pPvt->ringTail];
        pPvt->ringTail = (pPvt->ringTail==pPvt->ringSize) ? 0 : pPvt->ringTail+1;
        asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
            "%s %s::%s from ringBuffer value=%f\n",
                                            pPvt->pr->name, driverName, functionName, pPvt->result.value);
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


static long initAi(aiRecord *pai)
{
    int status;

    status = initCommon((dbCommon *)pai,&pai->inp,
        processCallbackInput,interruptCallbackInput);
    return status;
}

static long processAi(aiRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    asynStatus status;

    if (!getCallbackValue(pPvt) && !pr->pact) {
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
    recGblSetSevr(pr, pPvt->result.alarmStatus, pPvt->result.alarmSeverity);
    if(pPvt->result.status==asynSuccess) {
        epicsFloat64 val64 = pPvt->result.value;
        /* ASLO/AOFF conversion */
        if (pr->aslo != 0.0) val64 *= pr->aslo;
        val64 += pr->aoff;
        /* Smoothing */
        if (pr->smoo == 0.0 || pr->udf || !finite(pr->val))
            pr->val = val64;
        else
            pr->val = pr->val * pr->smoo + val64 * (1.0 - pr->smoo);
        pr->udf = 0;
        return 2;
    }
    else {
        return -1;
    }
}


static long initAo(aoRecord *pao)
{
    devPvt *pPvt;
    int status;
    epicsFloat64 value;

    status = initCommon((dbCommon *)pao,&pao->out,
        processCallbackOutput,interruptCallbackOutput);
    if (status != INIT_OK) return status;
    pPvt = pao->dpvt;
    /* Read the current value from the device */
    status = pasynFloat64SyncIO->read(pPvt->pasynUserSync,
                      &value,pPvt->pasynUser->timeout);
    if (status == asynSuccess) {
        epicsFloat64 val64 = value;
        /* ASLO/AOFF conversion */
        if (pao->aslo != 0.0) val64 *= pao->aslo;
        val64 += pao->aoff;
        pao->val = val64;
        pao->udf = 0;
    }
    pasynFloat64SyncIO->disconnect(pPvt->pasynUserSync);
    return INIT_DO_NOT_CONVERT; /* Do not convert */
}

static long processAo(aoRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    asynStatus status;

    epicsMutexLock(pPvt->devPvtLock);
    if (pPvt->newOutputCallbackValue && getCallbackValue(pPvt)) {
        if (pPvt->result.status == asynSuccess) {
            epicsFloat64 val64 = pPvt->result.value;
            /* ASLO/AOFF conversion */
            if (pr->aslo != 0.0) val64 *= pr->aslo;
            val64 += pr->aoff;
            pr->val = val64;
            pr->udf = 0;
        }
    } else if(pr->pact == 0) {
        /* ASLO/AOFF conversion */
        epicsFloat64 val64 = pr->oval - pr->aoff;
        if (pr->aslo != 0.0) val64 /= pr->aslo;
        pPvt->result.value = val64;
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
    recGblSetSevr(pr, pPvt->result.alarmStatus, pPvt->result.alarmSeverity);
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

static long initAiAverage(aiRecord *pai)
{
    int status;
    devPvt *pPvt;
    static const char *functionName="initAiAverage";

    status = initCommon((dbCommon *)pai,&pai->inp,
        0,interruptCallbackAverage);
    if (status != INIT_OK) return status;
    pPvt = pai->dpvt;
    pPvt->isAiAverage = 1;
    status = pPvt->pfloat64->registerInterruptUser(
                 pPvt->float64Pvt,pPvt->pasynUser,
                 pPvt->interruptCallback,pPvt,&pPvt->registrarPvt);
    if(status!=asynSuccess) {
        printf("%s %s::%s registerInterruptUser %s\n",
               pai->name,driverName, functionName,pPvt->pasynUser->errorMessage);
    }
    return INIT_OK;
}

static long processAiAverage(aiRecord *pai)
{
    devPvt *pPvt = (devPvt *)pai->dpvt;
    double dval;
    static const char *functionName="processAiAverage";

    epicsMutexLock(pPvt->devPvtLock);

    if (getCallbackValue(pPvt)) {
        /* Record is I/O Intr scanned and the average has been put in the ring buffer */
        dval = pPvt->result.value;
        pai->time = pPvt->result.time;
    } else {
        if (pPvt->numAverage == 0) {
            recGblSetSevr(pai, UDF_ALARM, INVALID_ALARM);
            pai->udf = 1;
            epicsMutexUnlock(pPvt->devPvtLock);
            return -2;
        }
        dval = pPvt->sum/pPvt->numAverage;
        pPvt->numAverage = 0;
        pPvt->sum = 0.;
    }
    epicsMutexUnlock(pPvt->devPvtLock);
    pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status,
                                            READ_ALARM, &pPvt->result.alarmStatus,
                                            INVALID_ALARM, &pPvt->result.alarmSeverity);
    recGblSetSevr(pai, pPvt->result.alarmStatus, pPvt->result.alarmSeverity);
    if (pPvt->result.status == asynSuccess) {
        /* ASLO/AOFF conversion */
        if (pai->aslo != 0.0) dval *= pai->aslo;
        dval += pai->aoff;
        /* Smoothing */
        if (pai->smoo == 0.0 || pai->udf || !finite(pai->val))
            pai->val = dval;
        else
            pai->val = pai->val * pai->smoo + dval * (1.0 - pai->smoo);
        pai->udf = 0;
        asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
                  "%s %s::%s val=%f\n",
                  pai->name, driverName, functionName,pai->val);
        return 2;
    }
    else {
        pPvt->result.status = asynSuccess;
        return -1;
    }
}
