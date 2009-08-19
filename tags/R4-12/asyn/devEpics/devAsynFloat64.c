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
#include <epicsRingBytes.h>
#include <epicsThread.h>
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
#include <epicsExport.h>

#define DEFAULT_RING_BUFFER_SIZE 10

typedef struct devPvt{
    dbCommon          *pr;
    asynUser          *pasynUser;
    asynUser          *pasynUserSync;
    asynFloat64       *pfloat64;
    void              *float64Pvt;
    void              *registrarPvt;
    int               canBlock;
    epicsMutexId      mutexId;
    asynStatus        status;
    epicsRingBytesId  ringBuffer;
    int               ringBufferSize;
    int               ringBufferOverflows;
    int               gotValue;
    epicsFloat64      value;
    epicsFloat64      sum;
    interruptCallbackFloat64 interruptCallback;
    int               numAverage;
    CALLBACK          callback;
    IOSCANPVT         ioScanPvt;
    char              *portName;
    char              *userParam;
    int               addr;
}devPvt;

static long initCommon(dbCommon *pr, DBLINK *plink,
    userCallback processCallback,interruptCallbackFloat64 interruptCallback);
static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt);
static void processCallbackInput(asynUser *pasynUser);
static void processCallbackOutput(asynUser *pasynUser);
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

    pPvt = callocMustSucceed(1, sizeof(*pPvt), "devAsynFloat64::initCommon");
    pr->dpvt = pPvt;
    pPvt->pr = pr;
    /* Create asynUser */
    pasynUser = pasynManager->createAsynUser(processCallback, 0);
    pasynUser->userPvt = pPvt;
    pPvt->pasynUser = pasynUser;
    pPvt->mutexId = epicsMutexCreate();
    /* Parse the link to get addr and port */
    status = pasynEpicsUtils->parseLink(pasynUser, plink,
                &pPvt->portName, &pPvt->addr,&pPvt->userParam);
    if (status != asynSuccess) {
        printf("%s devAsynFloat64::initCommon %s\n",
                     pr->name, pasynUser->errorMessage);
        goto bad;
    }
    /* Connect to device */
    status = pasynManager->connectDevice(pasynUser, pPvt->portName, pPvt->addr);
    if (status != asynSuccess) {
        printf("%s devAsynFloat64::initCommon connectDevice failed %s\n",
                     pr->name, pasynUser->errorMessage);
        goto bad;
    }
    status = pasynManager->canBlock(pPvt->pasynUser, &pPvt->canBlock);
    if (status != asynSuccess) {
        printf("%s devAsynFloat64::initCommon canBlock failed %s\n",
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
            printf("%s devAsynFloat64::initCommon drvUserCreate %s\n",
                     pr->name, pasynUser->errorMessage);
            goto bad;
        }
    }
    /* Get interface asynFloat64 */
    pasynInterface = pasynManager->findInterface(pasynUser, asynFloat64Type, 1);
    if (!pasynInterface) {
        printf("%s devAsynFloat64::initCommon findInterface asynFloat64Type %s\n",
                     pr->name,pasynUser->errorMessage);
        goto bad;
    }
    pPvt->pfloat64 = pasynInterface->pinterface;
    pPvt->float64Pvt = pasynInterface->drvPvt;

    /* Initialize synchronous interface */
    status = pasynFloat64SyncIO->connect(pPvt->portName, pPvt->addr,
                 &pPvt->pasynUserSync, pPvt->userParam);
    if (status != asynSuccess) {
        printf("%s devAsynFloat64::initCommon Float64SyncIO->connect failed %s\n",
               pr->name, pPvt->pasynUserSync->errorMessage);
        goto bad;
    }

    scanIoInit(&pPvt->ioScanPvt);
    pPvt->interruptCallback = interruptCallback;

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

    /* If initCommon failed then pPvt->pfloat64 is NULL, return error */
    if (!pPvt->pfloat64) return -1;

    if (cmd == 0) {
        /* Add to scan list.  Register interrupts, create ring buffer */
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s devAsynFloat64::getIoIntInfo registering interrupt\n",
            pr->name);
        if (!pPvt->ringBuffer) {
            DBENTRY *pdbentry = dbAllocEntry(pdbbase);
            pPvt->ringBufferSize = DEFAULT_RING_BUFFER_SIZE;
            status = dbFindRecord(pdbentry, pr->name);
            if (status)
                asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                    "%s devAsynFloat64::getIoIntInfo error finding record\n",
                    pr->name);
            sizeString = dbGetInfo(pdbentry, "FIFO");
            if (sizeString) pPvt->ringBufferSize = atoi(sizeString);
            pPvt->ringBuffer = epicsRingBytesCreate(pPvt->ringBufferSize*sizeof(epicsInt32));
            if (!pPvt->ringBuffer)
                asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                    "%s devAsynFloat64::getIoIntInfo error creating ring buffer\n",
                    pr->name);
            else
                asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
                    "%s devAsynFloat64::getIoIntInfo created ring buffer, size=%d\n",
                    pr->name, pPvt->ringBufferSize);
        }
        status = pPvt->pfloat64->registerInterruptUser(
           pPvt->float64Pvt,pPvt->pasynUser,
           pPvt->interruptCallback,pPvt,&pPvt->registrarPvt);
        if(status!=asynSuccess) {
            printf("%s devAsynFloat64 registerInterruptUser %s\n",
                   pr->name,pPvt->pasynUser->errorMessage);
        }
    } else {
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s devAsynFloat64::getIoIntInfo cancelling interrupt\n",
             pr->name);
        status = pPvt->pfloat64->cancelInterruptUser(pPvt->float64Pvt,
             pPvt->pasynUser,pPvt->registrarPvt);
        if(status!=asynSuccess) {
            printf("%s devAsynFloat64 cancelInterruptUser %s\n",
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
    int status=asynSuccess;

    status = pPvt->pfloat64->read(pPvt->float64Pvt, pPvt->pasynUser,
        &pPvt->value);
    if (status == asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
            "%s devAsynFloat64 process value=%f\n", pr->name,pPvt->value);
        pr->udf=0;
    } else {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "%s devAsynFloat64::process read error %s\n",
            pr->name, pasynUser->errorMessage);
        recGblSetSevr(pr, READ_ALARM, INVALID_ALARM);
    }
    if(pr->pact) callbackRequestProcessCallback(&pPvt->callback,pr->prio,pr);
}

static void processCallbackOutput(asynUser *pasynUser)
{
    devPvt *pPvt = (devPvt *)pasynUser->userPvt;
    dbCommon *pr = pPvt->pr;
    int status=asynSuccess;

    status = pPvt->pfloat64->write(pPvt->float64Pvt, pPvt->pasynUser,pPvt->value);
    if(status == asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
            "%s devAsynFloat64 process val %f\n",pr->name,pPvt->value);
    } else {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "%s devAsynFloat64 process error %s\n",
           pr->name, pasynUser->errorMessage);
       recGblSetSevr(pr, WRITE_ALARM, INVALID_ALARM);
    }
    pPvt->status = status;
    if(pr->pact) callbackRequestProcessCallback(&pPvt->callback,pr->prio,pr);
}

static void interruptCallbackInput(void *drvPvt, asynUser *pasynUser,
                epicsFloat64 value)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    dbCommon *pr = pPvt->pr;
    int count, size = sizeof(epicsFloat64);

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynFloat64::interruptCallbackInput new value=%f\n",
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
        pPvt->ringBufferOverflows++;
    }
    epicsMutexUnlock(pPvt->mutexId);
    scanIoRequest(pPvt->ioScanPvt);
}

static void interruptCallbackOutput(void *drvPvt, asynUser *pasynUser,
                epicsFloat64 value)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    dbCommon *pr = pPvt->pr;
    int count, size = sizeof(epicsFloat64);

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynFloat64::interruptCallbackOutput new value=%f\n",
        pr->name, value);
    epicsMutexLock(pPvt->mutexId);
    count = epicsRingBytesPut(pPvt->ringBuffer, (char *)&value, size);
    if (count != size) {
        pPvt->ringBufferOverflows++;
    }
    epicsMutexUnlock(pPvt->mutexId);
    scanOnce(pr);
}

static void interruptCallbackAverage(void *drvPvt, asynUser *pasynUser,
                epicsFloat64 value)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    dbCommon *pr = pPvt->pr;

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynFloat64::interruptCallbackAverage new value=%f\n",
        pr->name, value);
    epicsMutexLock(pPvt->mutexId);
    pPvt->numAverage++;
    pPvt->sum += value;
    epicsMutexUnlock(pPvt->mutexId);
}

static void getCallbackValue(devPvt *pPvt)
{
    int count, size=sizeof(epicsFloat64);

    epicsMutexLock(pPvt->mutexId);
    if (pPvt->ringBuffer && (epicsRingBytesUsedBytes(pPvt->ringBuffer) >= size)) {
        if (pPvt->ringBufferOverflows > 0) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynFloat64 getCallbackValue error, %d ring buffer overflows\n",
                pPvt->pr->name, pPvt->ringBufferOverflows);
                pPvt->ringBufferOverflows = 0;
        }
        count = epicsRingBytesGet(pPvt->ringBuffer, (char *)&pPvt->value, size);
        if (count == size) {
            asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
                "%s devAsynFloat64::getCallbackValue from ringBuffer value=%f\n",pPvt->pr->name,pPvt->value);
            pPvt->gotValue = 1;
        }
        else 
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynFloat64 getCallbackValue error, ring read failed\n",
                pPvt->pr->name);
    }
    epicsMutexUnlock(pPvt->mutexId);
}

static long initAi(aiRecord *pai)
{
    devPvt *pPvt = (devPvt *)pai->dpvt;
    asynStatus status;

    status = initCommon((dbCommon *)pai,&pai->inp,
        processCallbackInput,interruptCallbackInput);
    if(status != asynSuccess) return 0;
    pPvt = pai->dpvt;
    return(0);
}

static long processAi(aiRecord *pr)
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
                "%s devAsynFloat64 queueRequest %s\n",
                pr->name,pPvt->pasynUser->errorMessage);
            recGblSetSevr(pr, READ_ALARM, INVALID_ALARM);
        }
    }
    if(pPvt->status==asynSuccess) {
        pr->val = pPvt->value; pr->udf=0;
    }
    pPvt->gotValue = 0; pPvt->status = asynSuccess;
    return 2;
}


static long initAo(aoRecord *pao)
{
    devPvt *pPvt;
    asynStatus status;
    epicsFloat64 value;

    status = initCommon((dbCommon *)pao,&pao->out,
        processCallbackOutput,interruptCallbackOutput);
    if (status != asynSuccess) return 0;
    pPvt = pao->dpvt;
    /* Read the current value from the device */
    status = pasynFloat64SyncIO->read(pPvt->pasynUserSync,
                      &value,pPvt->pasynUser->timeout);
    if (status == asynSuccess) {
        pao->val = value;
        pao->udf = 0;
    }
    pasynFloat64SyncIO->disconnect(pPvt->pasynUserSync);
    return 2; /* Do not convert */
}

static long processAo(aoRecord *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    int status;

    getCallbackValue(pPvt);
    if(pPvt->gotValue) {
        /* This code is for I/O Intr scanned output records, which are not tested yet. */
        pr->val = pPvt->value; pr->udf = 0;
    } else if(pr->pact == 0) {
        pPvt->value = pr->oval;
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        if(status != asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynFloat64:process error queuing request %s\n",
                pr->name,pPvt->pasynUser->errorMessage);
            recGblSetSevr(pr, WRITE_ALARM, INVALID_ALARM);
        }
    }
    pPvt->gotValue = 0;
    return 0;
}

static long initAiAverage(aiRecord *pai)
{
    asynStatus status;
    devPvt *pPvt;

    status = initCommon((dbCommon *)pai,&pai->inp,
        0,interruptCallbackAverage);
    if (status != asynSuccess) return 0;
    pPvt = pai->dpvt;
    status = pPvt->pfloat64->registerInterruptUser(
                 pPvt->float64Pvt,pPvt->pasynUser,
                 pPvt->interruptCallback,pPvt,&pPvt->registrarPvt);
    if(status!=asynSuccess) {
        printf("%s devAsynFloat64 registerInterruptUser %s\n",
               pai->name,pPvt->pasynUser->errorMessage);
    }
    return(0);
}

static long processAiAverage(aiRecord *pai)
{
    devPvt *pPvt = (devPvt *)pai->dpvt;

    epicsMutexLock(pPvt->mutexId);
    if (pPvt->numAverage == 0) {
        recGblSetSevr(pai, UDF_ALARM, INVALID_ALARM);
        pai->udf = 1;
        return -2;
    }
    pai->udf = 0;
    pai->val = pPvt->sum/pPvt->numAverage;
    pPvt->numAverage = 0;
    pPvt->sum = 0.;
    epicsMutexUnlock(pPvt->mutexId);
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "%s devAsynFloat64::callbackAiAverage val=%f\n",
              pai->name, pai->val);
    return 2;
}
