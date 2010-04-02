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

#include <alarm.h>
#include <epicsAssert.h>
#include <recGbl.h>
#include <dbAccess.h>
#include <dbDefs.h>
#include <dbStaticLib.h>
#include <link.h>
#include <epicsPrint.h>
#include <epicsMutex.h>
#include <epicsRingBytes.h>
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
#include "asynEpicsUtils.h"
#include <epicsExport.h>

#define DEFAULT_RING_BUFFER_SIZE 10

typedef struct devPvt{
    dbCommon          *pr;
    asynUser          *pasynUser;
    asynUser          *pasynUserSync;
    asynUInt32Digital *puint32;
    void              *uint32Pvt;
    void              *registrarPvt;
    int               canBlock;
    epicsMutexId      mutexId;
    int               gotValue;
    epicsUInt32        value;
    epicsUInt32        mask;
    epicsRingBytesId  ringBuffer;
    int               ringBufferSize;
    int               ringBufferOverflows;
    interruptCallbackUInt32Digital interruptCallback;
    CALLBACK          callback;
    IOSCANPVT         ioScanPvt;
    char              *portName;
    char              *userParam;
    int               addr;
    asynStatus        status;
}devPvt;

#define NUM_BITS 16

static long initCommon(dbCommon *pr, DBLINK *plink,
    userCallback processCallback,interruptCallbackUInt32Digital interruptCallback);
static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt);
static void processCallbackInput(asynUser *pasynUser);
static void processCallbackOutput(asynUser *pasynUser);
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
    userCallback processCallback,interruptCallbackUInt32Digital interruptCallback)
{
    devPvt *pPvt;
    asynStatus status;
    asynUser *pasynUser;
    asynInterface *pasynInterface;

    pPvt = callocMustSucceed(1, sizeof(*pPvt), "devAsynUInt32Digital::initCommon");
    pr->dpvt = pPvt;
    pPvt->pr = pr;
    /* Create asynUser */
    pasynUser = pasynManager->createAsynUser(processCallback, 0);
    pasynUser->userPvt = pPvt;
    pPvt->pasynUser = pasynUser;
    pPvt->mutexId = epicsMutexCreate();
    /* Parse the link to get addr and port */
    status = pasynEpicsUtils->parseLinkMask(pasynUser, plink, 
                &pPvt->portName, &pPvt->addr, &pPvt->mask,&pPvt->userParam);
    if (status != asynSuccess) {
        printf("%s devAsynUInt32Digital::initCommon %s\n",
                     pr->name, pasynUser->errorMessage);
        goto bad;
    }
    /* Connect to device */
    status = pasynManager->connectDevice(pasynUser, pPvt->portName, pPvt->addr);
    if (status != asynSuccess) {
        printf("%s devAsynUInt32Digital::initCommon connectDevice failed %s\n",
                     pr->name, pasynUser->errorMessage);
        goto bad;
    }
    status = pasynManager->canBlock(pPvt->pasynUser, &pPvt->canBlock);
    if (status != asynSuccess) {
        printf("%s devAsynUInt32Digital::initCommon canBlock failed %s\n",
                     pr->name, pasynUser->errorMessage);
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
            printf("%s devAsynUInt32Digital::initCommon drvUserCreate %s\n",
                     pr->name, pasynUser->errorMessage);
            goto bad;
        }
    }
    /* Get interface asynUInt32Digital */
    pasynInterface = pasynManager->findInterface(pasynUser, asynUInt32DigitalType, 1);
    if (!pasynInterface) {
        printf("%s devAsynUInt32Digital::initCommon "
               "findInterface asynUInt32DigitalType %s\n",
                     pr->name,pasynUser->errorMessage);
        goto bad;
    }
    pPvt->puint32 = pasynInterface->pinterface;
    pPvt->uint32Pvt = pasynInterface->drvPvt;

    /* Initialize synchronous interface */
    status = pasynUInt32DigitalSyncIO->connect(pPvt->portName, pPvt->addr,
                 &pPvt->pasynUserSync, pPvt->userParam);
    if (status != asynSuccess) {
        printf("%s devAsynUInt32Digital::initCommon UInt32DigitalSyncIO->connect failed %s\n",
               pr->name, pPvt->pasynUserSync->errorMessage);
        goto bad;
    }

    pPvt->interruptCallback = interruptCallback;
    scanIoInit(&pPvt->ioScanPvt);
    return 0;
bad:
   pr->pact=1;
   return -1;
}

static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    asynStatus status;
    const char *sizeString;

    /* If initCommon failed then pPvt->puint32 is NULL, return error */
    if (!pPvt->puint32) return -1;

    if (cmd == 0) {
        /* Add to scan list.  Register interrupts */
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s devAsynUInt32Digital::getIoIntInfo registering interrupt\n",
            pr->name);
        if (!pPvt->ringBuffer) {
            DBENTRY *pdbentry = dbAllocEntry(pdbbase);
            pPvt->ringBufferSize = DEFAULT_RING_BUFFER_SIZE;
            status = dbFindRecord(pdbentry, pr->name);
            if (status)
                asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                    "%s devAsynUInt32Digital::getIoIntInfo error finding record\n",
                    pr->name);
            sizeString = dbGetInfo(pdbentry, "FIFO");
            if (sizeString) pPvt->ringBufferSize = atoi(sizeString);
            pPvt->ringBuffer = epicsRingBytesCreate(pPvt->ringBufferSize*sizeof(epicsInt32));
            if (!pPvt->ringBuffer)
                asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                    "%s devAsynUInt32Digital::getIoIntInfo error creating ring buffer\n",
                    pr->name);
            else
                asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
                    "%s devAsynUInt32Digital::getIoIntInfo created ring buffer, size=%d\n",
                    pr->name, pPvt->ringBufferSize);
        }
        status = pPvt->puint32->registerInterruptUser(
            pPvt->uint32Pvt,pPvt->pasynUser,
            pPvt->interruptCallback,pPvt,pPvt->mask,&pPvt->registrarPvt);
        if(status!=asynSuccess) {
            printf("%s devAsynUInt32Digital registerInterruptUser %s\n",
                   pr->name,pPvt->pasynUser->errorMessage);
        }
    } else {
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s devAsynUInt32Digital::getIoIntInfo cancelling interrupt\n",
             pr->name);
        status = pPvt->puint32->cancelInterruptUser(pPvt->uint32Pvt,
             pPvt->pasynUser,pPvt->registrarPvt);
        if(status!=asynSuccess) {
            printf("%s devAsynUInt32Digital cancelInterruptUser %s\n",
                   pr->name,pPvt->pasynUser->errorMessage);
        }
    }
    *iopvt = pPvt->ioScanPvt;
    return 0;
}

static void processCallbackInput(asynUser *pasynUser)
{
    devPvt *pPvt = (devPvt *)pasynUser->userPvt;
    dbCommon *pr = (dbCommon *)pPvt->pr;
    asynStatus status;

    status = pPvt->puint32->read(pPvt->uint32Pvt, pPvt->pasynUser,
        &pPvt->value,pPvt->mask);
    if (status == asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
            "%s devAsynUInt32Digital::process value=%lu\n",
            pr->name,pPvt->value);
    } else {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "%s devAsynUInt32Digital::process read error %s\n",
            pr->name, pasynUser->errorMessage);
        recGblSetSevr(pr, READ_ALARM, INVALID_ALARM);
    }
    pPvt->status = status;
    if(pr->pact) callbackRequestProcessCallback(&pPvt->callback,pr->prio,pr);
}

static void processCallbackOutput(asynUser *pasynUser)
{
    devPvt *pPvt = (devPvt *)pasynUser->userPvt;
    dbCommon *pr = pPvt->pr;
    int status=asynSuccess;

    status = pPvt->puint32->write(pPvt->uint32Pvt, pPvt->pasynUser,
        pPvt->value,pPvt->mask);
    if(status == asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
            "%s devAsynUInt32Digital process value %lu\n",pr->name,pPvt->value);
    } else {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "%s devAsynUInt32Digital process error %s\n",
           pr->name, pasynUser->errorMessage);
       recGblSetSevr(pr, WRITE_ALARM, INVALID_ALARM);
    }
    pPvt->status = status;
    if(pr->pact) callbackRequestProcessCallback(&pPvt->callback,pr->prio,pr);
}

static void interruptCallbackInput(void *drvPvt, asynUser *pasynUser,
                epicsUInt32 value)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    dbCommon *pr = pPvt->pr;
    int count, size = sizeof(epicsUInt32);

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynUInt32Digital::interruptCallbackInput new value=%lu\n",
        pr->name, value);
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
    /* Note that we put a lock around epicsRingBytesPut and Get because we potentially have
     * more than one reader, since the reader is whatever thread is processing the record */
    epicsMutexLock(pPvt->mutexId);
    count = epicsRingBytesPut(pPvt->ringBuffer, (char *)&value, size);
    if (count != size) {
        /* There was no room in the ring buffer.  In the past we just threw away
         * the new value.  However, it is better to remove the oldest value from the
         * ring buffer and add the new one.  That way the final value the record receives
         * is guaranteed to be the most recent value */
        epicsUInt32 dummy;
        count = epicsRingBytesGet(pPvt->ringBuffer, (char *)&dummy, size);
        if (count != size) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynUInt32Digital interruptCallbackInput error, ring read failed\n",
                pPvt->pr->name);
        }
        count = epicsRingBytesPut(pPvt->ringBuffer, (char *)&value, size);
        if (count != size) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynUInt32Digital interruptCallbackInput error, ring put failed\n",
                pPvt->pr->name);
        }
        pPvt->ringBufferOverflows++;
    } else {
        /* We only need to request the record to process if we added a 
         * new element to the ring buffer, not if we just replaced an element. */
        scanIoRequest(pPvt->ioScanPvt);
    }    
    epicsMutexUnlock(pPvt->mutexId);
}

static void interruptCallbackOutput(void *drvPvt, asynUser *pasynUser,
                epicsUInt32 value)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    dbCommon *pr = pPvt->pr;
    int count, size = sizeof(epicsUInt32);

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynUInt32Digital::interruptCallbackOutput new value=%lu\n",
        pr->name, value);
    epicsMutexLock(pPvt->mutexId);
    count = epicsRingBytesPut(pPvt->ringBuffer, (char *)&value, size);
    if (count != size) {
        pPvt->ringBufferOverflows++;
    }
    epicsMutexUnlock(pPvt->mutexId);
    scanOnce(pr);
}

static void getCallbackValue(devPvt *pPvt)
{
    int count, size=sizeof(epicsUInt32);

    epicsMutexLock(pPvt->mutexId);
    if (pPvt->ringBuffer && (epicsRingBytesUsedBytes(pPvt->ringBuffer) >= size)) {
        if (pPvt->ringBufferOverflows > 0) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynUInt32Digital getCallbackValue error, %d ring buffer overflows\n",
                pPvt->pr->name, pPvt->ringBufferOverflows);
                pPvt->ringBufferOverflows = 0;
        }
        count = epicsRingBytesGet(pPvt->ringBuffer, (char *)&pPvt->value, size);
        if (count == size) {
            asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
                "%s devAsynUInt32Digital::getCallbackValue from ringBuffer value=%lu\n",pPvt->pr->name,pPvt->value);
            pPvt->gotValue = 1;
        }
        else 
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynUInt32Digital getCallbackValue error, ring read failed\n",
                pPvt->pr->name);
    }
    epicsMutexUnlock(pPvt->mutexId);
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

static long initBi(biRecord *pr)
{
    devPvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pr,&pr->inp,
        processCallbackInput,interruptCallbackInput);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    pr->mask = pPvt->mask;
    return 0;
}
static long processBi(biRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    int status;

    getCallbackValue(pPvt);
    if(!pPvt->gotValue && !pr->pact) {
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        if(status != asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynUInt32Digital::process error queuing request %s\n", 
                pr->name,pPvt->pasynUser->errorMessage);
            recGblSetSevr(pr, READ_ALARM, INVALID_ALARM);
        }
    }
    if(pPvt->status==asynSuccess) {
        pr->rval = pPvt->value & pr->mask; pr->udf=0;
    }
    pPvt->gotValue = 0; pPvt->status = asynSuccess;
    return 0;
}

static long initBo(boRecord *pbo)
{
    devPvt *pPvt;
    asynStatus status;
    epicsUInt32 value;

    status = initCommon((dbCommon *)pbo,&pbo->out,
        processCallbackOutput,interruptCallbackOutput);
    if (status != asynSuccess) return 0;
    pPvt = pbo->dpvt;
    pbo->mask = pPvt->mask;
    /* Read the current value from the device */
    status = pasynUInt32DigitalSyncIO->read(pPvt->pasynUserSync,
                      &value, pPvt->mask,pPvt->pasynUser->timeout);
    pasynUInt32DigitalSyncIO->disconnect(pPvt->pasynUserSync);
    if (status == asynSuccess) {
        pbo->rval = value;
        return 0;
    }
    return 2; /* Do not convert */
}

static long processBo(boRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    int status;

    getCallbackValue(pPvt);
    if(pPvt->gotValue) {
        /* This code is for I/O Intr scanned output records, which are not tested yet. */
        pr->rval = pPvt->value & pr->mask;
        pr->val = (pr->rval) ? 1 : 0;
        pr->udf = 0;
    } else if(pr->pact == 0) {
        pPvt->value = pr->rval;;
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        if(status != asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynUInt32Digital:process error queuing request %s\n",
                pr->name,pPvt->pasynUser->errorMessage);
            recGblSetSevr(pr, WRITE_ALARM, INVALID_ALARM);
        }
    }
    pPvt->gotValue = 0;
    return 0;
}

static long initLi(longinRecord *pr)
{
    devPvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pr,&pr->inp,
        processCallbackInput,interruptCallbackInput);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    return 0;
}

static long processLi(longinRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    int status;

    getCallbackValue(pPvt);
    if(!pPvt->gotValue && !pr->pact) {
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        if(status != asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynUInt32Digital queueRequest %s\n", 
                pr->name,pPvt->pasynUser->errorMessage);
            recGblSetSevr(pr, READ_ALARM, INVALID_ALARM);
        }
    }
    if(pPvt->status==asynSuccess) {
        pr->val = pPvt->value; pr->udf=0;
    }
    pPvt->gotValue = 0; pPvt->status = asynSuccess;
    return 0;
}

static long initLo(longoutRecord *pr)
{
    devPvt *pPvt;
    asynStatus status;
    epicsUInt32 value;

    status = initCommon((dbCommon *)pr,&pr->out,
       processCallbackOutput,interruptCallbackOutput);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    /* Read the current value from the device */
    status = pasynUInt32DigitalSyncIO->read(pPvt->pasynUserSync,
                      &value, pPvt->mask,pPvt->pasynUser->timeout);
    if (status == asynSuccess) {
        pr->val = value;
        pr->udf = 0;
    }
    return 0;
}

static long processLo(longoutRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    int status;

    getCallbackValue(pPvt);
    if(pPvt->gotValue) {
        /* This code is for I/O Intr scanned output records, which are not tested yet. */
        pr->val = pPvt->value;
    } else if(pr->pact == 0) {
        pPvt->value = pr->val;;
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        if(status != asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynUInt32Digital::process error queuing request %s\n",
                pr->name,pPvt->pasynUser->errorMessage);
            recGblSetSevr(pr, WRITE_ALARM, INVALID_ALARM);
        }
    }
    pPvt->gotValue = 0;
    return 0;
}

static long initMbbi(mbbiRecord *pr)
{
    devPvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pr,&pr->inp,
        processCallbackInput,interruptCallbackInput);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    pr->mask = pPvt->mask;
    pr->shft = computeShift(pPvt->mask);
    return 0;
}

static long processMbbi(mbbiRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    int status;

    getCallbackValue(pPvt);
    if(!pPvt->gotValue && !pr->pact) {
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        if(status != asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynUInt32Digital queueRequest %s\n", 
                pr->name,pPvt->pasynUser->errorMessage);
            recGblSetSevr(pr, READ_ALARM, INVALID_ALARM);
        } 
    }
    if(pPvt->status==asynSuccess) {
        pr->rval = pPvt->value & pr->mask; pr->udf=0;
    }
    pPvt->gotValue = 0; pPvt->status = asynSuccess;
    return 0;
}

static long initMbbo(mbboRecord *pr)
{
    devPvt *pPvt;
    asynStatus status;
    epicsUInt32 value;

    status = initCommon((dbCommon *)pr,&pr->out,
        processCallbackOutput,interruptCallbackOutput);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    pr->mask = pPvt->mask;
    pr->shft = computeShift(pPvt->mask);
    /* Read the current value from the device */
    status = pasynUInt32DigitalSyncIO->read(pPvt->pasynUserSync,
                      &value, pPvt->mask, pPvt->pasynUser->timeout);
    if (status == asynSuccess) {
        pr->rval = value & pr->mask;
        return 0;
    }
    return 2;
}
static long processMbbo(mbboRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    int status;

    getCallbackValue(pPvt);
    if(pPvt->gotValue) {
        /* This code is for I/O Intr scanned output records, which are not tested yet. */
        unsigned long rval = pPvt->value & pr->mask;

        pr->rval = rval;
        if(pr->shft>0) rval >>= pr->shft;
        if(pr->sdef){
            epicsUInt32 *pstate_values;
            int           i;

            pstate_values = &(pr->zrvl);
            pr->val = 65535;        /* initalize to unknown state*/
            for (i = 0; i < 16; i++){
                    if (*pstate_values == rval){
                            pr->val = i;
                            break;
                    }
                    pstate_values++;
            }
        }else{
            /* the raw  is the desired val */
            pr->val =  (unsigned short)rval;
        }
        pr->udf = FALSE;
    } else if(pr->pact == 0) {
        pPvt->value = pr->rval;
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        if(status != asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynUInt32Digital::process error queuing request %s\n",
                pr->name,pPvt->pasynUser->errorMessage);
            recGblSetSevr(pr, WRITE_ALARM, INVALID_ALARM);
        }
    }
    pPvt->gotValue = 0;
    return 0;
}

static long initMbbiDirect(mbbiDirectRecord *pr)
{
    devPvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pr,&pr->inp,
        processCallbackInput,interruptCallbackInput);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    pr->mask = pPvt->mask;
    pr->shft = computeShift(pPvt->mask);
    return 0;
}

static long processMbbiDirect(mbbiDirectRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    int status;

    getCallbackValue(pPvt);
    if(!pPvt->gotValue && !pr->pact) {
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        if(status != asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynUInt32Digital queueRequest %s\n", 
                pr->name,pPvt->pasynUser->errorMessage);
            recGblSetSevr(pr, READ_ALARM, INVALID_ALARM);
        } 
    }
    if(pPvt->status==asynSuccess) {
        pr->rval = pPvt->value & pr->mask; pr->udf=0;
    }
    pPvt->gotValue = 0; pPvt->status = asynSuccess;
    return 0;
}

static long initMbboDirect(mbboDirectRecord *pr)
{
    devPvt *pPvt;
    asynStatus status;
    epicsUInt32 value;

    status = initCommon((dbCommon *)pr,&pr->out,
        processCallbackOutput,interruptCallbackOutput);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    pr->mask = pPvt->mask;
    pr->shft = computeShift(pPvt->mask);
    /* Read the current value from the device */
    status = pasynUInt32DigitalSyncIO->read(pPvt->pasynUserSync,
                      &value, pPvt->mask, pPvt->pasynUser->timeout);
    if (status == asynSuccess) {
        pr->rval = value & pr->mask;
        return 0;
    }
    return 2;
}
static long processMbboDirect(mbboDirectRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    int status;

    getCallbackValue(pPvt);
    if(pPvt->gotValue) {
        /* This code is for I/O Intr scanned output records, which are not tested yet. */
        epicsUInt32 rval = pPvt->value & pr->mask;
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
    } else if(pr->pact == 0) {
        pPvt->value = pr->rval;
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        if(status != asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynUInt32Digital::process error queuing request %s\n",
                pr->name,pPvt->pasynUser->errorMessage);
            recGblSetSevr(pr, WRITE_ALARM, INVALID_ALARM);
        }
    }
    pPvt->gotValue = 0;
    return 0;
}
