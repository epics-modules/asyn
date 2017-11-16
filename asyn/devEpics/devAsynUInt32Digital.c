/* devAsynUInt32Digital.c */
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
    15-OCT-2004
*/

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <alarm.h>
#include <epicsAssert.h>
#include <recGbl.h>
#include <dbAccess.h>
#include <dbDefs.h>
#include <dbEvent.h>
#include <dbStaticLib.h>
#include <link.h>
#include <epicsPrint.h>
#include <epicsMutex.h>
#include <epicsThread.h>
#include <cantProceed.h>
#include <dbCommon.h>
#include <dbScan.h>
#include <callback.h>
#include <biRecord.h>
#include <boRecord.h>
#include <longinRecord.h>
#include <longoutRecord.h>
#include <mbbiRecord.h>
#include <mbboRecord.h>
#include <mbbiDirectRecord.h>
#include <mbboDirectRecord.h>
#include <recSup.h>
#include <devSup.h>

#include <epicsExport.h>
#include "asynDriver.h"
#include "asynDrvUser.h"
#include "asynUInt32Digital.h"
#include "asynUInt32DigitalSyncIO.h"
#include "asynEnum.h"
#include "asynEnumSyncIO.h"
#include "asynEpicsUtils.h"

#define INIT_OK 0
#define INIT_DO_NOT_CONVERT 2
#define INIT_ERROR -1

#define DEFAULT_RING_BUFFER_SIZE 10
/* We should be getting these from db_access.h, but get errors including that file? */
#define MAX_ENUM_STATES 16
#define MAX_ENUM_STRING_SIZE 26

static const char *driverName = "devAsynUInt32Digital";

typedef struct ringBufferElement {
    epicsUInt32         value;
    epicsTimeStamp      time;
    asynStatus          status;
    epicsAlarmCondition alarmStatus;
    epicsAlarmSeverity  alarmSeverity;
} ringBufferElement;

typedef struct devPvt{
    dbCommon          *pr;
    asynUser          *pasynUser;
    asynUser          *pasynUserSync;
    asynUser          *pasynUserEnumSync;
    asynUInt32Digital *puint32;
    void              *uint32Pvt;
    void              *registrarPvt;
    int               canBlock;
    epicsMutexId      ringBufferLock;
    epicsUInt32        mask;
    ringBufferElement *ringBuffer;
    int               ringHead;
    int               ringTail;
    int               ringSize;
    int               ringBufferOverflows;
    ringBufferElement result;
    interruptCallbackUInt32Digital interruptCallback;
    CALLBACK          processCallback;
    CALLBACK          outputCallback;
    int               newOutputCallbackValue;
    int               numDeferredOutputCallbacks;
    IOSCANPVT         ioScanPvt;
    char              *portName;
    char              *userParam;
    int               addr;
    char              *enumStrings[MAX_ENUM_STATES];
    int               enumValues[MAX_ENUM_STATES];
    int               enumSeverities[MAX_ENUM_STATES];
    asynStatus        previousQueueRequestStatus;
}devPvt;

#define NUM_BITS 16

static void setEnums(char *outStrings, int *outVals, epicsEnum16 *outSeverities, 
                     char *inStrings[], int *inVals, int *inSeverities, 
                     size_t numIn, size_t numOut);
static long createRingBuffer(dbCommon *pr);
static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt);
static void processCallbackInput(asynUser *pasynUser);
static void processCallbackOutput(asynUser *pasynUser);
static void outputCallbackCallback(CALLBACK *pcb);
static int  getCallbackValue(devPvt *pPvt);
static void interruptCallbackInput(void *drvPvt, asynUser *pasynUser,
                epicsUInt32 value);
static void interruptCallbackOutput(void *drvPvt, asynUser *pasynUser,
                epicsUInt32 value);
static int computeShift(epicsUInt32 mask);

static long initBi(biRecord *pbi);
static long initBo(boRecord *pbo);
static long initLi(longinRecord *pli);
static long initLo(longoutRecord *plo);
static long initMbbi(mbbiRecord *pmbbi);
static long initMbbo(mbboRecord *pmbbo);
static long initMbbiDirect(mbbiDirectRecord *pmbbiDirect);
static long initMbboDirect(mbboDirectRecord *pmbboDirect);
/* process callbacks */
static long processBi(biRecord *pr);
static long processBo(boRecord *pr);
static long processLi(longinRecord *pr);
static long processLo(longoutRecord *pr);
static long processMbbi(mbbiRecord *pr);
static long processMbbo(mbboRecord *pr);
static long processMbbiDirect(mbbiDirectRecord *pr);
static long processMbboDirect(mbboDirectRecord *pr);

typedef struct analogDset { /* analog  dset */
    long          number;
    DEVSUPFUN     dev_report;
    DEVSUPFUN     init;
    DEVSUPFUN     init_record; 
    DEVSUPFUN     get_ioint_info;
    DEVSUPFUN     processCommon;/*(0)=>(success ) */
} analogDset;

analogDset asynBiUInt32Digital = {
    6,0,0,initBi,         getIoIntInfo, processBi};
analogDset asynBoUInt32Digital = {
    6,0,0,initBo,         getIoIntInfo, processBo};
analogDset asynLiUInt32Digital = {
    5,0,0,initLi,         getIoIntInfo, processLi};
analogDset asynLoUInt32Digital = {
    5,0,0,initLo,         getIoIntInfo, processLo};
analogDset asynMbbiUInt32Digital = {
    5,0,0,initMbbi,       getIoIntInfo, processMbbi};
analogDset asynMbboUInt32Digital = {
    5,0,0,initMbbo,       getIoIntInfo, processMbbo};
analogDset asynMbbiDirectUInt32Digital = {
    5,0,0,initMbbiDirect, getIoIntInfo, processMbbiDirect};
analogDset asynMbboDirectUInt32Digital = {
    5,0,0,initMbboDirect, getIoIntInfo, processMbboDirect};

epicsExportAddress(dset, asynBiUInt32Digital);
epicsExportAddress(dset, asynBoUInt32Digital);
epicsExportAddress(dset, asynLiUInt32Digital);
epicsExportAddress(dset, asynLoUInt32Digital);
epicsExportAddress(dset, asynMbbiUInt32Digital);
epicsExportAddress(dset, asynMbboUInt32Digital);
epicsExportAddress(dset, asynMbbiDirectUInt32Digital);
epicsExportAddress(dset, asynMbboDirectUInt32Digital);

static long initCommon(dbCommon *pr, DBLINK *plink,
    userCallback processCallback,interruptCallbackUInt32Digital interruptCallback, interruptCallbackEnum callbackEnum,
    int maxEnums, char *pFirstString, int *pFirstValue, epicsEnum16 *pFirstSeverity)
{
    devPvt *pPvt;
    asynStatus status;
    asynUser *pasynUser;
    asynInterface *pasynInterface;
    static const char *functionName="initCommon";

    pPvt = callocMustSucceed(1, sizeof(*pPvt), "devAsynUInt32Digital::initCommon");
    pr->dpvt = pPvt;
    pPvt->pr = pr;
    /* Create asynUser */
    pasynUser = pasynManager->createAsynUser(processCallback, 0);
    pasynUser->userPvt = pPvt;
    pPvt->pasynUser = pasynUser;
    pPvt->ringBufferLock = epicsMutexCreate();
    /* Parse the link to get addr and port */
    status = pasynEpicsUtils->parseLinkMask(pasynUser, plink, 
                &pPvt->portName, &pPvt->addr, &pPvt->mask,&pPvt->userParam);
    if (status != asynSuccess) {
        printf("%s %s::%s %s\n",
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
    /* Get interface asynUInt32Digital */
    pasynInterface = pasynManager->findInterface(pasynUser, asynUInt32DigitalType, 1);
    if (!pasynInterface) {
        printf("%s %s::%s findInterface asynUInt32DigitalType %s\n",
               pr->name, driverName, functionName,pasynUser->errorMessage);
        goto bad;
    }
    pPvt->puint32 = pasynInterface->pinterface;
    pPvt->uint32Pvt = pasynInterface->drvPvt;

    /* Initialize synchronous interface */
    status = pasynUInt32DigitalSyncIO->connect(pPvt->portName, pPvt->addr,
                 &pPvt->pasynUserSync, pPvt->userParam);
    if (status != asynSuccess) {
        printf("%s %s::%s UInt32DigitalSyncIO->connect failed %s\n",
               pr->name, driverName, functionName, pPvt->pasynUserSync->errorMessage);
        goto bad;
    }
    pPvt->interruptCallback = interruptCallback;
    scanIoInit(&pPvt->ioScanPvt);

    /* Initialize asynEnum interfaces */
    pasynInterface = pasynManager->findInterface(pPvt->pasynUser,asynEnumType,1);
    if (pasynInterface && (maxEnums > 0)) {
        size_t numRead;
        asynEnum *pasynEnum = pasynInterface->pinterface;
        void *registrarPvt;
        status = pasynEnumSyncIO->connect(pPvt->portName, pPvt->addr, 
                 &pPvt->pasynUserEnumSync, pPvt->userParam);
        if (status != asynSuccess) {
            printf("%s %s::%s EnumSyncIO->connect failed %s\n",
                   pr->name, driverName, functionName, pPvt->pasynUserEnumSync->errorMessage);
            goto bad;
        }
        status = pasynEnumSyncIO->read(pPvt->pasynUserEnumSync,
                    pPvt->enumStrings, pPvt->enumValues, pPvt->enumSeverities, maxEnums, 
                    &numRead, pPvt->pasynUser->timeout);
        if (status == asynSuccess) {
            setEnums(pFirstString, pFirstValue, pFirstSeverity, 
                     pPvt->enumStrings, pPvt->enumValues,  pPvt->enumSeverities, numRead, maxEnums);
        }
        status = pasynEnum->registerInterruptUser(
           pasynInterface->drvPvt, pPvt->pasynUser,
           callbackEnum, pPvt, &registrarPvt);
        if(status!=asynSuccess) {
            printf("%s %s::%s enum registerInterruptUser %s\n",
                   pr->name, driverName, functionName,pPvt->pasynUser->errorMessage);
        }
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
            status = pPvt->puint32->registerInterruptUser(
               pPvt->uint32Pvt,pPvt->pasynUser,
               pPvt->interruptCallback,pPvt,pPvt->mask, &pPvt->registrarPvt);
            if(status!=asynSuccess) {
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
        pPvt->ringBuffer = callocMustSucceed(pPvt->ringSize+1, sizeof *pPvt->ringBuffer, "%s::createRingBuffer");
    }
    return asynSuccess;
}


static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    asynStatus status;
    static const char *functionName="getIoIntInfo";

    /* If initCommon failed then pPvt->puint32 is NULL, return error */
    if (!pPvt->puint32) return -1;

    if (cmd == 0) {
        /* Add to scan list.  Register interrupts */
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s %s::%s registering interrupt\n",
            pr->name, driverName, functionName);
        createRingBuffer(pr);
        status = pPvt->puint32->registerInterruptUser(
            pPvt->uint32Pvt,pPvt->pasynUser,
            pPvt->interruptCallback,pPvt,pPvt->mask,&pPvt->registrarPvt);
        if(status!=asynSuccess) {
            printf("%s %s::%s registerInterruptUser %s\n",
                   pr->name, driverName, functionName,pPvt->pasynUser->errorMessage);
        }
    } else {
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s %s::%s cancelling interrupt\n",
             pr->name, driverName, functionName);
        status = pPvt->puint32->cancelInterruptUser(pPvt->uint32Pvt,
             pPvt->pasynUser,pPvt->registrarPvt);
        if(status!=asynSuccess) {
            printf("%s %s::%s cancelInterruptUser %s\n",
                   pr->name, driverName, functionName,pPvt->pasynUser->errorMessage);
        }
    }
    *iopvt = pPvt->ioScanPvt;
    return 0;
}

static void setEnums(char *outStrings, int *outVals, epicsEnum16 *outSeverities, char *inStrings[], int *inVals, int *inSeverities, 
                     size_t numIn, size_t numOut)
{
    size_t i;
    
    for (i=0; i<numOut; i++) {
        if (outStrings) outStrings[i*MAX_ENUM_STRING_SIZE] = '\0';
        if (outVals) outVals[i] = 0;
        if (outSeverities) outSeverities[i] = 0;
    }
    for (i=0; (i<numIn && i<numOut); i++) {
        if (outStrings) strncpy(&outStrings[i*MAX_ENUM_STRING_SIZE], inStrings[i], MAX_ENUM_STRING_SIZE);
        if (outVals) outVals[i] = inVals[i];
        if (outSeverities) outSeverities[i] = inSeverities[i];
    }
}

static void processCallbackInput(asynUser *pasynUser)
{
    devPvt *pPvt = (devPvt *)pasynUser->userPvt;
    dbCommon *pr = (dbCommon *)pPvt->pr;
    static const char *functionName="processCallbackInput";

    pPvt->result.status = pPvt->puint32->read(pPvt->uint32Pvt, pPvt->pasynUser,
        &pPvt->result.value,pPvt->mask);
    pPvt->result.time = pPvt->pasynUser->timestamp;
    pPvt->result.alarmStatus = pPvt->pasynUser->alarmStatus;
    pPvt->result.alarmSeverity = pPvt->pasynUser->alarmSeverity;
    if (pPvt->result.status == asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
            "%s %s::%s value=%u\n",
            pr->name, driverName, functionName,pPvt->result.value);
    } else {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "%s %s::%s read error %s\n",
            pr->name, driverName, functionName, pasynUser->errorMessage);
    }
    if(pr->pact) callbackRequestProcessCallback(&pPvt->processCallback,pr->prio,pr);
}

static void processCallbackOutput(asynUser *pasynUser)
{
    devPvt *pPvt = (devPvt *)pasynUser->userPvt;
    dbCommon *pr = pPvt->pr;
    static const char *functionName="processCallbackOutput";

    pPvt->result.status = pPvt->puint32->write(pPvt->uint32Pvt, pPvt->pasynUser,
        pPvt->result.value,pPvt->mask);
    pPvt->result.time = pPvt->pasynUser->timestamp;
    pPvt->result.alarmStatus = pPvt->pasynUser->alarmStatus;
    pPvt->result.alarmSeverity = pPvt->pasynUser->alarmSeverity;
    if(pPvt->result.status == asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
            "%s %s::%s process value %u\n",pr->name, driverName, functionName,pPvt->result.value);
    } else {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "%s %s::%s process error %s\n",
           pr->name, driverName, functionName, pasynUser->errorMessage);
    }
    if(pr->pact) callbackRequestProcessCallback(&pPvt->processCallback,pr->prio,pr);
}

static void interruptCallbackInput(void *drvPvt, asynUser *pasynUser,
                epicsUInt32 value)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    dbCommon *pr = pPvt->pr;
    ringBufferElement *rp;
    static const char *functionName="interruptCallbackInput";

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s %s::%s new value=%u\n",
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
    epicsMutexLock(pPvt->ringBufferLock);
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
        /* We only need to request the record to process if we added a 
         * new element to the ring buffer, not if we just replaced an element. */
        scanIoRequest(pPvt->ioScanPvt);
    }    
    epicsMutexUnlock(pPvt->ringBufferLock);
}

static void interruptCallbackOutput(void *drvPvt, asynUser *pasynUser,
                epicsUInt32 value)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    dbCommon *pr = pPvt->pr;
    ringBufferElement *rp;
    static const char *functionName="interruptCallbackOutput";

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s %s::%s new value=%u\n",
        pr->name, driverName, functionName, value);
    if (!interruptAccept) return;
    dbScanLock(pr);
    epicsMutexLock(pPvt->ringBufferLock);
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
        /* If PACT is true then this callback was received during asynchronous record processing
         * Must defer calling callbackRequest until end of record processing */
        if (pr->pact) {
            pPvt->numDeferredOutputCallbacks++;
        } else { 
            callbackRequest(&pPvt->outputCallback);
        }
    }
    epicsMutexUnlock(pPvt->ringBufferLock);
    dbScanUnlock(pr);
}

static void outputCallbackCallback(CALLBACK *pcb)
{
    devPvt *pPvt; 
    static const char *functionName="outputCallbackCallback";

    callbackGetUser(pPvt, pcb);
    {
        dbCommon *pr = pPvt->pr;
        dbScanLock(pr);
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
        dbScanUnlock(pr);
    }
}

static void interruptCallbackEnumMbbi(void *drvPvt, asynUser *pasynUser,
                char *strings[], int values[], int severities[],size_t nElements)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    mbbiRecord *pr = (mbbiRecord *)pPvt->pr;

    if (!interruptAccept) return;
    dbScanLock((dbCommon*)pr);
    setEnums((char*)&pr->zrst, (int*)&pr->zrvl, &pr->zrsv, 
             strings, values, severities, nElements, MAX_ENUM_STATES);
    db_post_events(pr, &pr->val, DBE_PROPERTY);
    dbScanUnlock((dbCommon*)pr);
}

static void interruptCallbackEnumMbbo(void *drvPvt, asynUser *pasynUser,
                char *strings[], int values[], int severities[], size_t nElements)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    mbboRecord *pr = (mbboRecord *)pPvt->pr;

    if (!interruptAccept) return;
    dbScanLock((dbCommon*)pr);
    setEnums((char*)&pr->zrst, (int*)&pr->zrvl, &pr->zrsv, 
             strings, values, severities, nElements, MAX_ENUM_STATES);
    db_post_events(pr, &pr->val, DBE_PROPERTY);
    dbScanUnlock((dbCommon*)pr);
}

static void interruptCallbackEnumBi(void *drvPvt, asynUser *pasynUser,
                char *strings[], int values[], int severities[], size_t nElements)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    biRecord *pr = (biRecord *)pPvt->pr;

    if (!interruptAccept) return;
    dbScanLock((dbCommon*)pr);
    setEnums((char*)&pr->znam, NULL, &pr->zsv, 
             strings, NULL, severities, nElements, 2);
    db_post_events(pr, &pr->val, DBE_PROPERTY);
    dbScanUnlock((dbCommon*)pr);
}

static void interruptCallbackEnumBo(void *drvPvt, asynUser *pasynUser,
                char *strings[], int values[], int severities[], size_t nElements)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    boRecord *pr = (boRecord *)pPvt->pr;

    if (!interruptAccept) return;
    dbScanLock((dbCommon*)pr);
    setEnums((char*)&pr->znam, NULL, &pr->zsv, 
             strings, NULL, severities, nElements, 2);
    db_post_events(pr, &pr->val, DBE_PROPERTY);
    dbScanUnlock((dbCommon*)pr);
}

static int getCallbackValue(devPvt *pPvt)
{
    int ret = 0;
    static const char *functionName="getCallbackValue";

    epicsMutexLock(pPvt->ringBufferLock);
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
            "%s %s::%s from ringBuffer value=%d\n",
                                            pPvt->pr->name, driverName, functionName,pPvt->result.value);
        ret = 1;
    }
    epicsMutexUnlock(pPvt->ringBufferLock);
    return ret;
}

static int computeShift(epicsUInt32 mask)
{
    epicsUInt32 bit=1;
    int i;

    for(i=0; i<32; i++, bit <<= 1 ) {
        if(mask&bit) break;
    }
    return i;
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


static long initBi(biRecord *pr)
{
    devPvt *pPvt;
    int status;

    status = initCommon((dbCommon *)pr,&pr->inp,
        processCallbackInput,interruptCallbackInput, interruptCallbackEnumBi,
        2, (char*)&pr->znam, NULL, &pr->zsv);
    if (status != INIT_OK) return status;
    pPvt = pr->dpvt;
    pr->mask = pPvt->mask;
    return INIT_OK;
}
static long processBi(biRecord *pr)
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
    recGblSetSevr(pr, pPvt->result.alarmStatus, pPvt->result.alarmSeverity);
    if(pPvt->result.status==asynSuccess) {
        pr->rval = pPvt->result.value & pr->mask; 
        pr->udf=0;
        return 0;
    }
    else {
        pPvt->result.status = asynSuccess;
        return -1;
    }
}

static long initBo(boRecord *pr)
{
    devPvt *pPvt;
    int status;
    epicsUInt32 value;

    status = initCommon((dbCommon *)pr,&pr->out,
        processCallbackOutput,interruptCallbackOutput, interruptCallbackEnumBo,
        2, (char*)&pr->znam, NULL, &pr->zsv);
    if (status != INIT_OK) return status;
    pPvt = pr->dpvt;
    pr->mask = pPvt->mask;
    /* Read the current value from the device */
    status = pasynUInt32DigitalSyncIO->read(pPvt->pasynUserSync,
                      &value, pPvt->mask,pPvt->pasynUser->timeout);
    pasynUInt32DigitalSyncIO->disconnect(pPvt->pasynUserSync);
    if (status == asynSuccess) {
        pr->rval = value;
        return INIT_OK;
    }
    return INIT_DO_NOT_CONVERT; /* Do not convert */
}

static long processBo(boRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    asynStatus status;

    if(pPvt->newOutputCallbackValue && getCallbackValue(pPvt)) {
        /* We got a callback from the driver */
        if (pPvt->result.status == asynSuccess) {
            pr->rval = pPvt->result.value & pr->mask;
            pr->val = (pr->rval) ? 1 : 0;
            pr->udf = 0;
        }
    } else if(pr->pact == 0) {
        pPvt->result.value = pr->rval;;
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
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
        processCallbackInput,interruptCallbackInput, NULL,
        0, NULL, NULL, NULL);
    if (status != INIT_OK) return status;
    return INIT_OK;
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
    recGblSetSevr(pr, pPvt->result.alarmStatus, pPvt->result.alarmSeverity);
    if(pPvt->result.status==asynSuccess) {
        pr->val = pPvt->result.value & pPvt->mask;
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
    epicsUInt32 value;

    status = initCommon((dbCommon *)pr,&pr->out,
       processCallbackOutput,interruptCallbackOutput, NULL,
       0, NULL, NULL, NULL);
    if (status != INIT_OK) return status;
    pPvt = pr->dpvt;
    /* Read the current value from the device */
    status = pasynUInt32DigitalSyncIO->read(pPvt->pasynUserSync,
                      &value, pPvt->mask,pPvt->pasynUser->timeout);
    if (status == asynSuccess) {
        pr->val = value;
        pr->udf = 0;
    }
    return INIT_OK;
}

static long processLo(longoutRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    asynStatus status;

    if(pPvt->newOutputCallbackValue && getCallbackValue(pPvt)) {
        /* We got a callback from the driver */
        if (pPvt->result.status == asynSuccess) {
            pr->val = pPvt->result.value & pPvt->mask;
            pr->udf = 0;
        }
    } else if(pr->pact == 0) {
        pPvt->result.value = pr->val & pPvt->mask;
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
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
    if(pPvt->result.status == asynSuccess) {
        return 0;
    }
    else {
        return -1;
    }
}

static long initMbbi(mbbiRecord *pr)
{
    devPvt *pPvt;
    int status;

    status = initCommon((dbCommon *)pr,&pr->inp,
        processCallbackInput,interruptCallbackInput, interruptCallbackEnumMbbi,
        MAX_ENUM_STATES, (char*)&pr->zrst, (int*)&pr->zrvl, &pr->zrsv);
    if (status != INIT_OK) return status;
    pPvt = pr->dpvt;
    pr->mask = pPvt->mask;
    pr->shft = computeShift(pPvt->mask);
    return INIT_OK;
}

static long processMbbi(mbbiRecord *pr)
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
    recGblSetSevr(pr, pPvt->result.alarmStatus, pPvt->result.alarmSeverity);
    if(pPvt->result.status==asynSuccess) {
        pr->rval = pPvt->result.value & pr->mask; 
        pr->udf=0;
        return 0;
    }
    else {
        pPvt->result.status = asynSuccess;
        return -1;
    }
}

static long initMbbo(mbboRecord *pr)
{
    devPvt *pPvt;
    int status;
    epicsUInt32 value;

    status = initCommon((dbCommon *)pr,&pr->out,
        processCallbackOutput,interruptCallbackOutput, interruptCallbackEnumMbbo,
        MAX_ENUM_STATES, (char*)&pr->zrst, (int*)&pr->zrvl, &pr->zrsv);
    if (status != INIT_OK) return status;
    pPvt = pr->dpvt;
    pr->mask = pPvt->mask;
    pr->shft = computeShift(pPvt->mask);
    /* Read the current value from the device */
    status = pasynUInt32DigitalSyncIO->read(pPvt->pasynUserSync,
                      &value, pPvt->mask, pPvt->pasynUser->timeout);
    if (status == asynSuccess) {
        pr->rval = value & pr->mask;
        return INIT_OK;
    }
    return INIT_DO_NOT_CONVERT;
}
static long processMbbo(mbboRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    asynStatus status;

    if (pPvt->newOutputCallbackValue && getCallbackValue(pPvt)) {
        /* We got a callback from the driver */
        if (pPvt->result.status == asynSuccess) {
            unsigned long rval = pPvt->result.value & pr->mask;
            pr->rval = rval;
            if(pr->shft>0) rval >>= pr->shft;
            if(pr->sdef){
                epicsUInt32 *pstate_values;
                int i;
                pstate_values = &(pr->zrvl);
                pr->val = 65535;        /* initalize to unknown state*/
                for (i=0; i<16; i++){
                    if (*pstate_values == rval){
                        pr->val = i;
                        break;
                    }
                    pstate_values++;
                }
            } else {
                /* the raw  is the desired val */
                pr->val =  (unsigned short)rval;
            }
            pr->udf = FALSE;
        }
    } else if(pr->pact == 0) {
        pPvt->result.value = pr->rval;
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
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
    if(pPvt->result.status == asynSuccess) {
        return 0;
    }
    else {
        pPvt->result.status = asynSuccess;
        return -1;
    }
}

static long initMbbiDirect(mbbiDirectRecord *pr)
{
    devPvt *pPvt;
    int status;

    status = initCommon((dbCommon *)pr,&pr->inp,
        processCallbackInput,interruptCallbackInput, NULL,
        0, NULL, NULL, NULL);
    if (status != INIT_OK) return status;
    pPvt = pr->dpvt;
    pr->mask = pPvt->mask;
    pr->shft = computeShift(pPvt->mask);
    return INIT_OK;
}

static long processMbbiDirect(mbbiDirectRecord *pr)
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
    recGblSetSevr(pr, pPvt->result.alarmStatus, pPvt->result.alarmSeverity);
    if(pPvt->result.status==asynSuccess) {
        pr->rval = pPvt->result.value & pr->mask; 
        pr->udf=0;
        return 0;
    }
    else {
        pPvt->result.status = asynSuccess;
        return -1;
    }
}

static long initMbboDirect(mbboDirectRecord *pr)
{
    devPvt *pPvt;
    int status;
    epicsUInt32 value;

    status = initCommon((dbCommon *)pr,&pr->out,
        processCallbackOutput,interruptCallbackOutput, NULL,
        0, NULL, NULL, NULL);
    if (status != INIT_OK) return status;
    pPvt = pr->dpvt;
    pr->mask = pPvt->mask;
    pr->shft = computeShift(pPvt->mask);

    /* Read the current value from the device */
    status = pasynUInt32DigitalSyncIO->read(pPvt->pasynUserSync,
                      &value, pPvt->mask, pPvt->pasynUser->timeout);
    if (status == asynSuccess) {
        epicsUInt8 *pBn = &pr->b0;
        int i;

        value &= pr->mask;
        if (pr->shft > 0) value >>= pr->shft;
        pr->val =  (unsigned short) value;
        pr->udf = FALSE;
        for (i = 0; i < 16; i++) {
            *pBn++ = !! (value & 1);
            value >>= 1;
        }
    }
    return INIT_DO_NOT_CONVERT;
}
static long processMbboDirect(mbboDirectRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    asynStatus status;

    if (pPvt->newOutputCallbackValue && getCallbackValue(pPvt)) {
        /* We got a callback from the driver */
        if (pPvt->result.status == asynSuccess) {
            epicsUInt32 rval = pPvt->result.value & pr->mask;
            unsigned char *bit = &(pr->b0);
            int i, offset=1;

            pr->rval = rval;
            if(pr->shft>0) rval >>= pr->shft;
            for (i=0; i<NUM_BITS; i++, offset <<= 1, bit++ ) {
                if(*bit) {
                    pr->val |= offset;
                } else {
                    pr->val &= ~offset;
                }
            }
        }
    } else if(pr->pact == 0) {
        pPvt->result.value = pr->rval;
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
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
    if(pPvt->result.status == asynSuccess) {
        return 0;
    }
    else {
        pPvt->result.status = asynSuccess;
        return -1;
    }
}
