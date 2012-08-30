/* devAsynInt32.c */
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
#include <aiRecord.h>
#include <cvtTable.h>
#include <aoRecord.h>
#include <menuConvert.h>
#include <longinRecord.h>
#include <longoutRecord.h>
#include <biRecord.h>
#include <boRecord.h>
#include <mbbiRecord.h>
#include <mbboRecord.h>
#include <recSup.h>
#include <devSup.h>

#include <epicsExport.h>
#include "asynDriver.h"
#include "asynDrvUser.h"
#include "asynInt32.h"
#include "asynInt32SyncIO.h"
#include "asynEnum.h"
#include "asynEnumSyncIO.h"
#include "asynEpicsUtils.h"
#include <epicsExport.h>

#define DEFAULT_RING_BUFFER_SIZE 10
/* We should be getting these from db_access.h, but get errors including that file? */
#define MAX_ENUM_STATES 16
#define MAX_ENUM_STRING_SIZE 26

typedef struct ringBufferElement {
    epicsInt32      value;
    epicsTimeStamp  time;
    asynStatus      status;
} ringBufferElement;

typedef struct devInt32Pvt{
    dbCommon          *pr;
    asynUser          *pasynUser;
    asynUser          *pasynUserSync;
    asynUser          *pasynUserEnumSync;
    asynInt32         *pint32;
    void              *int32Pvt;
    void              *registrarPvt;
    int               canBlock;
    epicsInt32        deviceLow;
    epicsInt32        deviceHigh;
    epicsMutexId      mutexId;
    epicsAlarmCondition alarmStat;
    epicsAlarmSeverity alarmSevr;
    ringBufferElement *ringBuffer;
    int               ringHead;
    int               ringTail;
    int               ringSize;
    int               ringBufferOverflows;
    ringBufferElement result;
    interruptCallbackInt32 interruptCallback;
    double            sum;
    int               numAverage;
    int               bipolar;
    epicsInt32        mask;
    epicsInt32        signBit;
    CALLBACK          callback;
    IOSCANPVT         ioScanPvt;
    char              *portName;
    char              *userParam;
    int               addr;
    char              *enumStrings[MAX_ENUM_STATES];
    int               enumValues[MAX_ENUM_STATES];
    int               enumSeverities[MAX_ENUM_STATES];
}devInt32Pvt;

static void setEnums(char *outStrings, int *outVals, epicsEnum16 *outSeverities, 
                     char *inStrings[], int *inVals, int *inSeverities, 
                     size_t numIn, size_t numOut);
static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt);
static long convertAi(aiRecord *pai, int pass);
static long convertAo(aoRecord *pao, int pass);
static void processCallbackInput(asynUser *pasynUser);
static void processCallbackOutput(asynUser *pasynUser);
static void interruptCallbackInput(void *drvPvt, asynUser *pasynUser,
                epicsInt32 value);
static void interruptCallbackOutput(void *drvPvt, asynUser *pasynUser,
                epicsInt32 value);
static void interruptCallbackAverage(void *drvPvt, asynUser *pasynUser,
                epicsInt32 value);

static long initAi(aiRecord *pai);
static long initAiAverage(aiRecord *pai);
static long initAo(aoRecord *pao);
static long initLi(longinRecord *pli);
static long initLo(longoutRecord *plo);
static long initBi(biRecord *pbi);
static long initBo(boRecord *pbo);
static long initMbbi(mbbiRecord *pmbbi);
static long initMbbo(mbboRecord *pmbbo);
static long processAi(aiRecord *pr);
static long processAiAverage(aiRecord *pr);
static long processAo(aoRecord *pr);
static long processLi(longinRecord *pr);
static long processLo(longoutRecord *pr);
static long processBi(biRecord *pr);
static long processBo(boRecord *pr);
static long processMbbi(mbbiRecord *pr);
static long processMbbo(mbboRecord *pr);

typedef struct analogDset { /* analog  dset */
    long          number;
    DEVSUPFUN     dev_report;
    DEVSUPFUN     init;
    DEVSUPFUN     init_record;
    DEVSUPFUN     get_ioint_info;
    DEVSUPFUN     processCommon;/*(0)=>(success ) */
    DEVSUPFUN     special_linconv;
} analogDset;

analogDset asynAiInt32 = {
    6,0,0,initAi,       getIoIntInfo, processAi, convertAi };
analogDset asynAiInt32Average = {
    6,0,0,initAiAverage,getIoIntInfo, processAiAverage , convertAi };
analogDset asynAoInt32 = {
    6,0,0,initAo,       getIoIntInfo, processAo , convertAo };
analogDset asynLiInt32 = {
    5,0,0,initLi,       getIoIntInfo, processLi };
analogDset asynLoInt32 = {
    5,0,0,initLo,       getIoIntInfo, processLo };
analogDset asynBiInt32 = {
    5,0,0,initBi,     getIoIntInfo, processBi };
analogDset asynBoInt32 = {
    5,0,0,initBo,     getIoIntInfo, processBo };
analogDset asynMbbiInt32 = {
    5,0,0,initMbbi,     getIoIntInfo, processMbbi };
analogDset asynMbboInt32 = {
    5,0,0,initMbbo,     getIoIntInfo, processMbbo };

epicsExportAddress(dset, asynAiInt32);
epicsExportAddress(dset, asynAiInt32Average);
epicsExportAddress(dset, asynAoInt32);
epicsExportAddress(dset, asynLiInt32);
epicsExportAddress(dset, asynLoInt32);
epicsExportAddress(dset, asynBiInt32);
epicsExportAddress(dset, asynBoInt32);
epicsExportAddress(dset, asynMbbiInt32);
epicsExportAddress(dset, asynMbboInt32);

static long initCommon(dbCommon *pr, DBLINK *plink,
    userCallback processCallback,interruptCallbackInt32 interruptCallback, interruptCallbackEnum callbackEnum,
    int maxEnums, char *pFirstString, int *pFirstValue, epicsEnum16 *pFirstSeverity)
{
    devInt32Pvt *pPvt;
    asynStatus status;
    asynUser *pasynUser;
    asynInterface *pasynInterface;
    epicsUInt32 mask=0;
    int nbits;

    pPvt = callocMustSucceed(1, sizeof(*pPvt), "devAsynInt32::initCommon");
    pr->dpvt = pPvt;
    pPvt->pr = pr;
    /* Create asynUser */
    pasynUser = pasynManager->createAsynUser(processCallback, 0);
    pasynUser->userPvt = pPvt;
    pPvt->pasynUser = pasynUser;
    pPvt->mutexId = epicsMutexCreate();
 
    /* Parse the link to get addr and port */
    /* We accept 2 different link syntax (@asyn(...) and @asynMask(...)
     * If parseLink returns an error then try parseLinkMask. */
    status = pasynEpicsUtils->parseLink(pasynUser, plink, 
                &pPvt->portName, &pPvt->addr, &pPvt->userParam);
    if (status != asynSuccess) {
        status = pasynEpicsUtils->parseLinkMask(pasynUser, plink, 
                &pPvt->portName, &pPvt->addr, &mask, &pPvt->userParam);
    }
    if (status != asynSuccess) {
        printf("%s devAsynInt32::initCommon  %s\n",
                     pr->name, pasynUser->errorMessage);
        goto bad;
    }
    
    /* Parse nbits if it was specified */
    nbits = (int)mask;
    if (nbits) {
        if (nbits < 0) {
            nbits = -nbits;
            pPvt->bipolar = 1;
        }
        pPvt->signBit = (epicsInt32) ldexp(1.0, nbits-1);
        pPvt->mask = pPvt->signBit*2 - 1;
        if (pPvt->bipolar) {
            pPvt->deviceLow = ~(pPvt->mask/2)+1;
            pPvt->deviceHigh = (pPvt->mask/2);
        } else {
            pPvt->deviceLow = 0;
            pPvt->deviceHigh = pPvt->mask;
        }
    }
            
    /* Connect to device */
    status = pasynManager->connectDevice(pasynUser, pPvt->portName, pPvt->addr);
    if (status != asynSuccess) {
        printf("%s devAsynInt32::initCommon connectDevice failed %s\n",
                     pr->name, pasynUser->errorMessage);
        goto bad;
    }
    status = pasynManager->canBlock(pPvt->pasynUser, &pPvt->canBlock);
    if (status != asynSuccess) {
        printf("%s devAsynInt32::initCommon canBlock failed %s\n",
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
            printf("%s devAsynInt32::initCommon drvUserCreate %s\n",
                     pr->name, pasynUser->errorMessage);
            goto bad;
        }
    }
    /* Get interface asynInt32 */
    pasynInterface = pasynManager->findInterface(pasynUser, asynInt32Type, 1);
    if (!pasynInterface) {
        printf("%s devAsynInt32::initCommon findInterface asynInt32Type %s\n",
                     pr->name,pasynUser->errorMessage);
        goto bad;
    }
    pPvt->pint32 = pasynInterface->pinterface;
    pPvt->int32Pvt = pasynInterface->drvPvt;
    scanIoInit(&pPvt->ioScanPvt);
    pPvt->interruptCallback = interruptCallback;
    /* Initialize synchronous interface */
    status = pasynInt32SyncIO->connect(pPvt->portName, pPvt->addr, 
                 &pPvt->pasynUserSync, pPvt->userParam);
    if (status != asynSuccess) {
        printf("%s devAsynInt32::initCommon Int32SyncIO->connect failed %s\n",
               pr->name, pPvt->pasynUserSync->errorMessage);
        goto bad;
    }
    /* Initialize asynEnum interfaces */
    pasynInterface = pasynManager->findInterface(pPvt->pasynUser,asynEnumType,1);
    if (pasynInterface && (maxEnums > 0)) {
        size_t numRead;
        asynEnum *pasynEnum = pasynInterface->pinterface;
        void *registrarPvt;
        status = pasynEnumSyncIO->connect(pPvt->portName, pPvt->addr, 
                 &pPvt->pasynUserEnumSync, pPvt->userParam);
        if (status != asynSuccess) {
            printf("%s devAsynInt32::initCommon EnumSyncIO->connect failed %s\n",
                   pr->name, pPvt->pasynUserEnumSync->errorMessage);
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
            printf("%s devAsynInt32 enum registerInterruptUser %s\n",
                   pr->name,pPvt->pasynUser->errorMessage);
        }
    }
    return 0;
bad:
   pr->pact=1;
   return -1;
}

static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pr->dpvt;
    asynStatus status;
    const char *sizeString;

    /* If initCommon failed then pPvt->pint32 is NULL, return error */
    if (!pPvt->pint32) return -1;

    if (cmd == 0) {
        /* Add to scan list.  Register interrupts */
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s devAsynInt32::getIoIntInfo registering interrupt\n",
            pr->name);
        if (!pPvt->ringBuffer) {
            DBENTRY *pdbentry = dbAllocEntry(pdbbase);
            pPvt->ringSize = DEFAULT_RING_BUFFER_SIZE;
            status = dbFindRecord(pdbentry, pr->name);
            if (status)
                asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                    "%s devAsynInt32::getIoIntInfo error finding record\n",
                    pr->name);
            sizeString = dbGetInfo(pdbentry, "FIFO");
            if (sizeString) pPvt->ringSize = atoi(sizeString);
            pPvt->ringBuffer = callocMustSucceed(pPvt->ringSize+1, sizeof *pPvt->ringBuffer, "devAsynInt32::getIoIntInfo");
        }
        status = pPvt->pint32->registerInterruptUser(
           pPvt->int32Pvt,pPvt->pasynUser,
           pPvt->interruptCallback,pPvt,&pPvt->registrarPvt);
        if(status!=asynSuccess) {
            printf("%s devAsynInt32 registerInterruptUser %s\n",
                   pr->name,pPvt->pasynUser->errorMessage);
        }
    } else {
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s devAsynInt32::getIoIntInfo cancelling interrupt\n",
             pr->name);
        status = pPvt->pint32->cancelInterruptUser(pPvt->int32Pvt,
             pPvt->pasynUser,pPvt->registrarPvt);
        if(status!=asynSuccess) {
            printf("%s devAsynInt32 cancelInterruptUser %s\n",
                   pr->name,pPvt->pasynUser->errorMessage);
        }
    }
    *iopvt = pPvt->ioScanPvt;
    return 0;
}

static void setEnums(char *outStrings, int *outVals, epicsEnum16 *outSeverities, char *inStrings[], int *inVals, int *inSeverities, 
                     size_t numIn, size_t numOut)
{
    int i;
    
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

static long convertAi(aiRecord *precord, int pass)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)precord->dpvt;
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
    devInt32Pvt *pPvt = (devInt32Pvt *)precord->dpvt;
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
    devInt32Pvt *pPvt = (devInt32Pvt *)pasynUser->userPvt;
    dbCommon *pr = (dbCommon *)pPvt->pr;

    pPvt->result.status = pPvt->pint32->read(pPvt->int32Pvt, pPvt->pasynUser, &pPvt->result.value);
    if (pPvt->bipolar && (pPvt->result.value & pPvt->signBit)) pPvt->result.value |= ~pPvt->mask;
    if (pPvt->result.status == asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
            "%s devAsynInt32 process value=%d\n",pr->name,pPvt->result.value);
    } else {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "%s devAsynInt32 process read error %s\n",
              pr->name, pasynUser->errorMessage);
    }
    if(pr->pact) callbackRequestProcessCallback(&pPvt->callback,pr->prio,pr);
}

static void processCallbackOutput(asynUser *pasynUser)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pasynUser->userPvt;
    dbCommon *pr = pPvt->pr;

    pPvt->result.status = pPvt->pint32->write(pPvt->int32Pvt, pPvt->pasynUser,pPvt->result.value);
    if(pPvt->result.status == asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
            "%s devAsynInt32 process value %d\n",pr->name,pPvt->result.value);
    } else {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "%s devAsynInt32 process error %s\n",
           pr->name, pasynUser->errorMessage);
    }
    if(pr->pact) callbackRequestProcessCallback(&pPvt->callback,pr->prio,pr);
}

static void interruptCallbackInput(void *drvPvt, asynUser *pasynUser, 
                epicsInt32 value)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)drvPvt;
    dbCommon *pr = pPvt->pr;
    ringBufferElement *rp;

    if (pPvt->bipolar && (value & pPvt->signBit)) value |= ~pPvt->mask;
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynInt32::interruptCallbackInput new value=%d\n",
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
    epicsMutexLock(pPvt->mutexId);
    rp = &pPvt->ringBuffer[pPvt->ringHead];
    rp->value = value;
    rp->time = pasynUser->timestamp;
    rp->status = pasynUser->auxStatus;
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
    epicsMutexUnlock(pPvt->mutexId);
}

static void interruptCallbackOutput(void *drvPvt, asynUser *pasynUser,
                epicsInt32 value)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)drvPvt;
    dbCommon *pr = pPvt->pr;
    ringBufferElement *rp;
    int nextHead;

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynInt32::interruptCallbackOutput new value=%d\n",
        pr->name, value);
    epicsMutexLock(pPvt->mutexId);
    nextHead = (pPvt->ringHead==pPvt->ringSize) ? 0 : pPvt->ringHead+1;
    if (nextHead == pPvt->ringTail) {
        pPvt->ringBufferOverflows++;
    }
    else {
        rp = &pPvt->ringBuffer[pPvt->ringHead];
        rp->value = value;
        rp->time = pasynUser->timestamp;
        rp->status = pasynUser->auxStatus;
        pPvt->ringHead = nextHead;
    }
    epicsMutexUnlock(pPvt->mutexId);
    scanOnce(pr);
}

static void interruptCallbackAverage(void *drvPvt, asynUser *pasynUser,
                epicsInt32 value)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)drvPvt;
    aiRecord *pai = (aiRecord *)pPvt->pr;

    if (pPvt->bipolar && (value & pPvt->signBit)) value |= ~pPvt->mask;
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynInt32::interruptCallbackAverage new value=%d\n",
         pai->name, value);
    epicsMutexLock(pPvt->mutexId);
    pPvt->numAverage++; 
    pPvt->sum += (double)value;
    pPvt->result.status |= pasynUser->auxStatus;
    epicsMutexUnlock(pPvt->mutexId);
}

static void interruptCallbackEnumMbbi(void *drvPvt, asynUser *pasynUser,
                char *strings[], int values[], int severities[],size_t nElements)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)drvPvt;
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
    devInt32Pvt *pPvt = (devInt32Pvt *)drvPvt;
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
    devInt32Pvt *pPvt = (devInt32Pvt *)drvPvt;
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
    devInt32Pvt *pPvt = (devInt32Pvt *)drvPvt;
    boRecord *pr = (boRecord *)pPvt->pr;

    if (!interruptAccept) return;
    dbScanLock((dbCommon*)pr);
    setEnums((char*)&pr->znam, NULL, &pr->zsv, 
             strings, NULL, severities, nElements, 2);
    db_post_events(pr, &pr->val, DBE_PROPERTY);
    dbScanUnlock((dbCommon*)pr);
}


static int
getCallbackValue(devInt32Pvt *pPvt)
{
    int ret = 0;
    epicsMutexLock(pPvt->mutexId);
    if (pPvt->ringTail != pPvt->ringHead) {
        if (pPvt->ringBufferOverflows > 0) {
            asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
                "%s devAsynInt32 getCallbackValue warning, %d ring buffer overflows\n",
                                    pPvt->pr->name, pPvt->ringBufferOverflows);
            pPvt->ringBufferOverflows = 0;
        }
        pPvt->result = pPvt->ringBuffer[pPvt->ringTail];
        pPvt->ringTail = (pPvt->ringTail==pPvt->ringSize) ? 0 : pPvt->ringTail+1;
        asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
            "%s devAsynInt32::getCallbackValue from ringBuffer value=%d\n",
                                            pPvt->pr->name,pPvt->result.value);
        ret = 1;
    }
    epicsMutexUnlock(pPvt->mutexId);
    return ret;
}

static long initAi(aiRecord *pr)
{
    devInt32Pvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pr,&pr->inp,
        processCallbackInput,interruptCallbackInput, NULL,
        0, NULL, NULL, NULL);
    if(status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    /* Don't call getBounds if we already have non-zero values from
     * parseLinkMask */
    if ((pPvt->deviceLow == 0) && (pPvt->deviceHigh == 0)) {
        pasynInt32SyncIO->getBounds(pPvt->pasynUserSync,
                                &pPvt->deviceLow, &pPvt->deviceHigh);
    }
    convertAi(pr, 1);
    return 0;
}
static long processAi(aiRecord *pr)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pr->dpvt;
    int status;

    if(!getCallbackValue(pPvt) && !pr->pact) {
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        if(status != asynSuccess) {
            pPvt->result.status = status;
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynInt32 queueRequest %s\n", 
                pr->name,pPvt->pasynUser->errorMessage);
        }
    }
    pr->rval = pPvt->result.value; 
    pr->time = pPvt->result.time; 
    if (pPvt->result.status == asynSuccess) {
        pr->udf = 0;
    }
    else {
        pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status, READ_ALARM, &pPvt->alarmStat,
                                            INVALID_ALARM, &pPvt->alarmSevr);
        (void)recGblSetSevr(pr, pPvt->alarmStat, pPvt->alarmSevr);
    }
    return 0;
}

static long initAiAverage(aiRecord *pr)
{
    devInt32Pvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pr, &pr->inp,
        NULL, interruptCallbackAverage, NULL, 
        0, NULL, NULL, NULL);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    status = pPvt->pint32->registerInterruptUser(
                 pPvt->int32Pvt,pPvt->pasynUser,
                 interruptCallbackAverage,pPvt,&pPvt->registrarPvt);
    if(status!=asynSuccess) {
        printf("%s devAsynInt32 registerInterruptUser %s\n",
               pr->name,pPvt->pasynUser->errorMessage);
    }
    /* Don't call getBounds if we already have non-zero values from
     * parseLinkMask */
    if ((pPvt->deviceLow == 0) && (pPvt->deviceHigh == 0)) {
        pasynInt32SyncIO->getBounds(pPvt->pasynUserSync,
                                &pPvt->deviceLow, &pPvt->deviceHigh);
    }
    convertAi(pr, 1);
    return 0;
}

static long processAiAverage(aiRecord *pr)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pr->dpvt;
    double rval;

    epicsMutexLock(pPvt->mutexId);
    if (pPvt->numAverage == 0) {
        (void)recGblSetSevr(pr, UDF_ALARM, INVALID_ALARM);
        pr->udf = 1;
        epicsMutexUnlock(pPvt->mutexId);
        return -2;
    }
    rval = pPvt->sum/pPvt->numAverage;
    /*round result*/
    rval += (pPvt->sum>0.0) ? 0.5 : -0.5;
    pr->rval = (epicsInt32) rval;
    pPvt->numAverage = 0;
    pPvt->sum = 0.;
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynInt32::processAiInt32Average rval=%d, status=%d\n",pr->name, pr->rval, pPvt->result.status);
    if (pPvt->result.status == asynSuccess) {
        pr->udf = 0;
    }
    else {
        pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status, READ_ALARM, &pPvt->alarmStat,
                                                INVALID_ALARM, &pPvt->alarmSevr);
        (void)recGblSetSevr(pr, pPvt->alarmStat, pPvt->alarmSevr);
        pPvt->result.status = 0;
    }
    epicsMutexUnlock(pPvt->mutexId);
    return 0;
}

static long initAo(aoRecord *pao)
{
    devInt32Pvt *pPvt;
    asynStatus status;
    epicsInt32 value;

    status = initCommon((dbCommon *)pao,&pao->out,
        processCallbackOutput,interruptCallbackOutput, NULL,
        0, NULL, NULL, NULL);
    if (status != asynSuccess) return 0;
    pPvt = pao->dpvt;
    /* Don't call getBounds if we already have non-zero values from
     * parseLinkMask */
    if ((pPvt->deviceLow == 0) && (pPvt->deviceHigh == 0)) {
        pasynInt32SyncIO->getBounds(pPvt->pasynUserSync,
                                &pPvt->deviceLow, &pPvt->deviceHigh);
    }
    convertAo(pao, 1);
    /* Read the current value from the device */
    status = pasynInt32SyncIO->read(pPvt->pasynUserSync,
                      &value, pPvt->pasynUser->timeout);
    if (pPvt->bipolar && (value & pPvt->signBit)) value |= ~pPvt->mask;
    if (status == asynSuccess) {
        pao->rval = value;
        return 0;
    }
    return 2; /* Do not convert */
}

static long processAo(aoRecord *pr)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pr->dpvt;
    asynStatus status;
    double     value;
    
    if (getCallbackValue(pPvt)) {
        /* This code is for I/O Intr scanned output records, which are not tested yet. */
        pr->rval = pPvt->result.value;
        pr->udf = 0;
        value = (double)pr->rval + (double)pr->roff;
        if(pr->aslo!=0.0) value *= pr->aslo;
        value += pr->aoff;
        if (pr->linr == menuConvertNO_CONVERSION){
            ; /*do nothing*/
        } else if ((pr->linr == menuConvertLINEAR) ||
                  (pr->linr == menuConvertSLOPE)) {
            value = value*pr->eslo + pr->eoff;
        }else{
            if(cvtRawToEngBpt(&value,pr->linr,pr->init,
                    (void *)&pr->pbrk,&pr->lbrk)!=0) {
                asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                    "%s devAsynInt32 cvtRawToEngBpt failed\n",
                    pr->name);
                (void)recGblSetSevr(pr, WRITE_ALARM, INVALID_ALARM);
                goto done;
            }
        }
        pr->val = value;
        pr->udf = isnan(value);
    } else if(pr->pact == 0) {
        pPvt->result.value = pr->rval;
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        if(status != asynSuccess) {
            pPvt->result.status = status;
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynInt32 queueRequest %s\n",
                pr->name,pPvt->pasynUser->errorMessage);
        }
    }
    if(pPvt->result.status != asynSuccess) {
        pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status, WRITE_ALARM, &pPvt->alarmStat,
                                                INVALID_ALARM, &pPvt->alarmSevr);
        (void)recGblSetSevr(pr, pPvt->alarmStat, pPvt->alarmSevr);
    }
done:
    return 0;
}

static long initLi(longinRecord *pr)
{
    asynStatus status;

    status = initCommon((dbCommon *)pr,&pr->inp,
       processCallbackInput,interruptCallbackInput, NULL,
       0, NULL, NULL, NULL);

    if (status != asynSuccess) return 0;
    return 0;
}

static long processLi(longinRecord *pr)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pr->dpvt;
    int status;

    if(!getCallbackValue(pPvt) && !pr->pact) {
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        if(status != asynSuccess) {
            pPvt->result.status = status;
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynInt32 queueRequest %s\n", 
                pr->name,pPvt->pasynUser->errorMessage);
        }
    }
    pr->val = pPvt->result.value;
    pr->time = pPvt->result.time;
    if(pPvt->result.status==asynSuccess) {
        pr->udf=0;
    }
    else {
        pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status, READ_ALARM, &pPvt->alarmStat,
                                                INVALID_ALARM, &pPvt->alarmSevr);
        (void)recGblSetSevr(pr, pPvt->alarmStat, pPvt->alarmSevr);
    }
    pPvt->result.status = asynSuccess;
    return 0;
}

static long initLo(longoutRecord *pr)
{
    devInt32Pvt *pPvt;
    asynStatus status;
    epicsInt32 value;

    status = initCommon((dbCommon *)pr,&pr->out,
        processCallbackOutput,interruptCallbackOutput, NULL,
        0, NULL, NULL, NULL);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    /* Read the current value from the device */
    status = pasynInt32SyncIO->read(pPvt->pasynUserSync,
                      &value, pPvt->pasynUser->timeout);
    if (status == asynSuccess) {
        pr->val = value;
        pr->udf = 0;
    }
    return 0;
}

static long processLo(longoutRecord *pr)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pr->dpvt;
    int status;

    if (getCallbackValue(pPvt)) {
        /* This code is for I/O Intr scanned output records, which are not tested yet. */
        pr->val = pPvt->result.value; pr->udf = 0;
    } else if(pr->pact == 0) {
        pPvt->result.value = pr->val;
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        if(status != asynSuccess) {
            pPvt->result.status = status;
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynInt32 queueRequest %s\n",
                pr->name,pPvt->pasynUser->errorMessage);
        }
    }
    if(pPvt->result.status != asynSuccess) {
        pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status, WRITE_ALARM, &pPvt->alarmStat,
                                                INVALID_ALARM, &pPvt->alarmSevr);
        (void)recGblSetSevr(pr, pPvt->alarmStat, pPvt->alarmSevr);
    }
    return 0;
}


static long initBi(biRecord *pr)
{
    devInt32Pvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pr,&pr->inp,
        processCallbackInput,interruptCallbackInput, interruptCallbackEnumBi,
        2, (char*)&pr->znam, NULL, &pr->zsv);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    return 0;
}

static long processBi(biRecord *pr)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pr->dpvt;
    int status;

    if(!getCallbackValue(pPvt) && !pr->pact) {
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        if(status != asynSuccess) {
            pPvt->result.status = status;
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynInt32 queueRequest %s\n", 
                pr->name,pPvt->pasynUser->errorMessage);
        } 
    }
    pr->rval = pPvt->result.value;
    pr->time = pPvt->result.time;
    if(pPvt->result.status==asynSuccess) {
        pr->udf=0;
    }
    else {
        pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status, READ_ALARM, &pPvt->alarmStat,
                                                INVALID_ALARM, &pPvt->alarmSevr);
        (void)recGblSetSevr(pr, pPvt->alarmStat, pPvt->alarmSevr);
    }
    pPvt->result.status = asynSuccess;
    return 0;
}

static long initBo(boRecord *pr)
{
    devInt32Pvt *pPvt;
    asynStatus status;
    epicsInt32 value;

    status = initCommon((dbCommon *)pr,&pr->out,
        processCallbackOutput,interruptCallbackOutput, interruptCallbackEnumBo,
        2, (char*)&pr->znam, NULL, &pr->zsv);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    /* Read the current value from the device */
    status = pasynInt32SyncIO->read(pPvt->pasynUserSync,
                      &value, pPvt->pasynUser->timeout);
    if (status == asynSuccess) {
        pr->rval = value;
        return 0;
    }
    return 2;
}

static long processBo(boRecord *pr)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pr->dpvt;
    int status;

    if(getCallbackValue(pPvt)) {
        /* This code is for I/O Intr scanned output records, which are not tested yet. */
        pr->rval = pPvt->result.value;
        pr->val = (pr->rval) ? 1 : 0;
        pr->udf = 0;
    } else if(pr->pact == 0) {
        pPvt->result.value = pr->rval;
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        if(status != asynSuccess) {
            pPvt->result.status = status;
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynInt32::processCommon, error queuing request %s\n",
                pr->name,pPvt->pasynUser->errorMessage);
        }
    }
    if(pPvt->result.status != asynSuccess) {
        pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status, WRITE_ALARM, &pPvt->alarmStat,
                                                INVALID_ALARM, &pPvt->alarmSevr);
        (void)recGblSetSevr(pr, pPvt->alarmStat, pPvt->alarmSevr);
    }
    return 0;
}

static long initMbbi(mbbiRecord *pr)
{
    devInt32Pvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pr,&pr->inp,
        processCallbackInput,interruptCallbackInput, interruptCallbackEnumMbbi,
        MAX_ENUM_STATES, (char*)&pr->zrst, (int*)&pr->zrvl, &pr->zrsv);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    if(pr->nobt == 0) pr->mask = 0xffffffff;
    pr->mask <<= pr->shft;
    return 0;
}

static long processMbbi(mbbiRecord *pr)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pr->dpvt;
    int status;

    if(!getCallbackValue(pPvt) && !pr->pact) {
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        if(status != asynSuccess) {
            pPvt->result.status = status;
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynInt32 queueRequest %s\n", 
                pr->name,pPvt->pasynUser->errorMessage);
        } 
    }
    pr->rval = pPvt->result.value & pr->mask;
    pr->time = pPvt->result.time;
    if(pPvt->result.status==asynSuccess) {
        pr->udf=0;
    }
    else {
        pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status, READ_ALARM, &pPvt->alarmStat,
                                                INVALID_ALARM, &pPvt->alarmSevr);
        (void)recGblSetSevr(pr, pPvt->alarmStat, pPvt->alarmSevr);
    }
    pPvt->result.status = asynSuccess;
    return 0;
}

static long initMbbo(mbboRecord *pr)
{
    devInt32Pvt *pPvt;
    asynStatus status;
    epicsInt32 value;

    status = initCommon((dbCommon *)pr,&pr->out,
        processCallbackOutput,interruptCallbackOutput, interruptCallbackEnumMbbo,
        MAX_ENUM_STATES, (char*)&pr->zrst, (int*)&pr->zrvl, &pr->zrsv);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    if(pr->nobt == 0) pr->mask = 0xffffffff;
    pr->mask <<= pr->shft;
    /* Read the current value from the device */
    status = pasynInt32SyncIO->read(pPvt->pasynUserSync,
                      &value, pPvt->pasynUser->timeout);
    if (status == asynSuccess) {
        pr->rval = value & pr->mask;
        return 0;
    }
    return 2;
}
static long processMbbo(mbboRecord *pr)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pr->dpvt;
    int status;

    if(getCallbackValue(pPvt)) {
        /* This code is for I/O Intr scanned output records, which are not tested yet. */
        unsigned long rval = pPvt->result.value & pr->mask;

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
        pPvt->result.value = pr->rval;
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        if(status != asynSuccess) {
            pPvt->result.status = status;
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynInt32::processCommon, error queuing request %s\n",
                pr->name,pPvt->pasynUser->errorMessage);
        }
    }
    if(pPvt->result.status != asynSuccess) {
        pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status, WRITE_ALARM, &pPvt->alarmStat,
                                                INVALID_ALARM, &pPvt->alarmSevr);
        (void)recGblSetSevr(pr, pPvt->alarmStat, pPvt->alarmSevr);
    }
    return 0;
}
