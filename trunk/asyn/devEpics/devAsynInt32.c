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

#include <alarm.h>
#include <recGbl.h>
#include <dbAccess.h>
#include <dbDefs.h>
#include <link.h>
#include <epicsPrint.h>
#include <epicsExport.h>
#include <epicsMutex.h>
#include <cantProceed.h>
#include <dbCommon.h>
#include <dbScan.h>
#include <callback.h>
#include <aiRecord.h>
#include <aoRecord.h>
#include <longinRecord.h>
#include <longoutRecord.h>
#include <mbbiRecord.h>
#include <mbboRecord.h>
#include <recSup.h>
#include <devSup.h>

#include "asynDriver.h"
#include "asynDrvUser.h"
#include "asynInt32.h"
#include "asynInt32SyncIO.h"
#include "asynEpicsUtils.h"

typedef struct devInt32Pvt{
    dbCommon          *pr;
    asynUser          *pasynUser;
    asynInt32         *pint32;
    void              *int32Pvt;
    void              *registrarPvt;
    int               canBlock;
    epicsInt32        deviceLow;
    epicsInt32        deviceHigh;
    epicsMutexId      mutexId;
    int               gotValue; /*For callbackInterrupt */
    double            sum;
    epicsInt32        value;
    int               numAverage;
    CALLBACK          callback;
    IOSCANPVT         ioScanPvt;
    char              *portName;
    char              *userParam;
    int               addr;
}devInt32Pvt;

static long initCommon(dbCommon *pr, DBLINK *plink, userCallback callback);
static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt);
static long convertAi(aiRecord *pai, int pass);
static long convertAo(aoRecord *pao, int pass);
/* asyn callbacks */
static void callbackInterrupt(void *drvPvt, epicsInt32 value);
static void callbackAiAverage(void *drvPvt, epicsInt32 value);

static long initAi(aiRecord *pai);
static long initAiAverage(aiRecord *pai);
static long initAo(aoRecord *pao);
static long initLi(longinRecord *pli);
static long initLo(longoutRecord *plo);
static long initMbbi(mbbiRecord *pmbbi);
static long initMbbo(mbboRecord *pmbbo);
/* process callbacks */
static long processAi(aiRecord *pr);
static long processAiAverage(aiRecord *pr);
static long processAo(aoRecord *pr);
static long processLi(longinRecord *pr);
static long processLo(longoutRecord *pr);
static long processMbbi(mbbiRecord *pr);
static long processMbbo(mbboRecord *pr);
/*queue process Callbacks*/
static void processCallbackAi(asynUser *pasynUser);
static void processCallbackAo(asynUser *pasynUser);
static void processCallbackLi(asynUser *pasynUser);
static void processCallbackLo(asynUser *pasynUser);
static void processCallbackMbbi(asynUser *pasynUser);
static void processCallbackMbbo(asynUser *pasynUser);

typedef struct analogDset { /* analog  dset */
    long          number;
    DEVSUPFUN     dev_report;
    DEVSUPFUN     init;
    DEVSUPFUN     init_record; /*returns: (0,2)=>(success,success no convert)*/
    DEVSUPFUN     get_ioint_info;
    DEVSUPFUN     processCommon;/*(0)=>(success ) */
    DEVSUPFUN     special_linconv;
} analogDset;

analogDset asynAiInt32 = {
    6,0,0,initAi,       getIoIntInfo,processAi, convertAi };
analogDset asynAiInt32Average = {
    6,0,0,initAiAverage,getIoIntInfo, processAiAverage , convertAi };
analogDset asynAoInt32 = {
    6,0,0,initAo,       0,           processAo , convertAo };
analogDset asynLiInt32 = {
    5,0,0,initLi,       getIoIntInfo, processLi };
analogDset asynLoInt32 = {
    5,0,0,initLo,       0,            processLo };
analogDset asynMbbiInt32 = {
    5,0,0,initMbbi,     getIoIntInfo, processMbbi };
analogDset asynMbboInt32 = {
    5,0,0,initMbbo,     0,            processMbbo };

epicsExportAddress(dset, asynAiInt32);
epicsExportAddress(dset, asynAiInt32Average);
epicsExportAddress(dset, asynAoInt32);
epicsExportAddress(dset, asynLiInt32);
epicsExportAddress(dset, asynLoInt32);
epicsExportAddress(dset, asynMbbiInt32);
epicsExportAddress(dset, asynMbboInt32);

static long initCommon(dbCommon *pr, DBLINK *plink, userCallback callback)
{
    devInt32Pvt *pPvt;
    asynStatus status;
    asynUser *pasynUser;
    asynInterface *pasynInterface;

    pPvt = callocMustSucceed(1, sizeof(*pPvt), "devAsynInt32::initCommon");
    pr->dpvt = pPvt;
    pPvt->pr = pr;
    /* Create asynUser */
    pasynUser = pasynManager->createAsynUser(callback, 0);
    pasynUser->userPvt = pPvt;
    pPvt->pasynUser = pasynUser;
    pPvt->mutexId = epicsMutexCreate();
    /* Parse the link to get addr and port */
    status = pasynEpicsUtils->parseLink(pasynUser, plink, 
                &pPvt->portName, &pPvt->addr, &pPvt->userParam);
    if (status != asynSuccess) {
        printf("devAsynInt32::initCommon, %s error in link %s\n",
                     pr->name, pasynUser->errorMessage);
        goto bad;
    }
    /* Connect to device */
    status = pasynManager->connectDevice(pasynUser, pPvt->portName, pPvt->addr);
    if (status != asynSuccess) {
        printf("devAsynInt32::initCommon, %s connectDevice failed %s\n",
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
            printf("devAsynInt32::initCommon, %s drvUserCreate failed %s\n",
                     pr->name, pasynUser->errorMessage);
            goto bad;
        }
    }
    /* Get interface asynInt32 */
    pasynInterface = pasynManager->findInterface(pasynUser, asynInt32Type, 1);
    if (!pasynInterface) {
        printf("devAsynInt32::initCommon, %s findInterface asynInt32Type failed\n",
                     pr->name);
        goto bad;
    }
    pPvt->pint32 = pasynInterface->pinterface;
    pPvt->int32Pvt = pasynInterface->drvPvt;
    return 0;

bad:
   pr->pact=1;
   return(-1);
}

static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pr->dpvt;

    *iopvt = pPvt->ioScanPvt;
    return 0;
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

static void callbackInterrupt(void *drvPvt, epicsInt32 value)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)drvPvt;
    dbCommon *pr = pPvt->pr;

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynInt32::callbackAiInt32Interrupt new value=%d\n",
        pr->name, value);
    epicsMutexLock(pPvt->mutexId);
    pPvt->gotValue = 1; pPvt->value = value;
    epicsMutexUnlock(pPvt->mutexId);
    scanIoRequest(pPvt->ioScanPvt);
}
static void callbackAiAverage(void *drvPvt, epicsInt32 value)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)drvPvt;
    aiRecord *pai = (aiRecord *)pPvt->pr;

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynInt32::callbackAiAverage new value=%d\n",
         pai->name, value);
    epicsMutexLock(pPvt->mutexId);
    pPvt->numAverage++; pPvt->sum += (double)value;
    epicsMutexUnlock(pPvt->mutexId);
}

static long initAi(aiRecord *pr)
{
    devInt32Pvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pr,&pr->inp, processCallbackAi);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    pasynInt32SyncIO->getBoundsOnce(pPvt->portName,pPvt->addr,
                            &pPvt->deviceLow, &pPvt->deviceHigh);
    convertAi(pr, 1);
    pasynManager->canBlock(pPvt->pasynUser, &pPvt->canBlock);
    scanIoInit(&pPvt->ioScanPvt);
    status = pPvt->pint32->registerInterruptUser(pPvt->int32Pvt,pPvt->pasynUser,
        callbackInterrupt,pPvt,&pPvt->registrarPvt);
    if(status!=asynSuccess) {
        printf("%s devAsynInt32 registerInterruptUser failed %s\n",
            pr->name,pPvt->pasynUser->errorMessage);
    }
    return 0;
}
static long processAi(aiRecord *pr)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pr->dpvt;
    int status;

    if(pPvt->gotValue) {
        pr->rval = pPvt->value;
        pPvt->gotValue = 0;
    } else {
        if(pr->pact == 0) {
            status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
            if((status==asynSuccess) && pPvt->canBlock) {
                 pr->pact = 1;
                 return 0;
            }
            if(status != asynSuccess) {
                asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                    "%s devAsynInt32::processCommon, error queuing request %s\n", 
                    pr->name,pPvt->pasynUser->errorMessage);
            }
        }
    }
    return 0;
}

static void processCallbackAi(asynUser *pasynUser)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pasynUser->userPvt;
    dbCommon *pr = (dbCommon *)pPvt->pr;
    int status=asynSuccess;

    status = pPvt->pint32->read(pPvt->int32Pvt, pPvt->pasynUser, &pPvt->value);
    if (status == asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
            "%s devAsynInt32::processAiInt32  rval=%d\n",pr->name,pPvt->value);
        pr->udf=0;
    } else {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "%s devAsynInt32::processAiInt32 read error %s\n",
              pr->name, pasynUser->errorMessage);
        recGblSetSevr(pr, READ_ALARM, INVALID_ALARM);
    }
    if(pr->pact) callbackRequestProcessCallback(&pPvt->callback,pr->prio,pr);
}

static long initAiAverage(aiRecord *pr)
{
    devInt32Pvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pr,&pr->inp,0);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    pasynInt32SyncIO->getBoundsOnce(pPvt->portName,pPvt->addr,
                                &pPvt->deviceLow, &pPvt->deviceHigh);
    convertAi(pr, 1);
    scanIoInit(&pPvt->ioScanPvt);
    status = pPvt->pint32->registerInterruptUser(pPvt->int32Pvt,pPvt->pasynUser,
        callbackAiAverage,pPvt,&pPvt->registrarPvt);
    if(status!=asynSuccess) {
        printf("%s devAsynInt32 registerInterruptUser failed %s\n",
            pr->name,pPvt->pasynUser->errorMessage);
    }
    return 0;
}

static long processAiAverage(aiRecord *pr)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pr->dpvt;
    double rval;

    epicsMutexLock(pPvt->mutexId);
    if (pPvt->numAverage == 0) pPvt->numAverage = 1;
    rval = pPvt->sum/pPvt->numAverage;
    /*round result*/
    rval += (pPvt->sum>0.0) ? 0.5 : -0.5;
    pr->rval = rval;
    pPvt->numAverage = 0;
    pPvt->sum = 0.;
    epicsMutexUnlock(pPvt->mutexId);
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynInt32::processAiInt32Average rval=%d\n",pr->name, pr->rval);
    pr->udf = 0;
    return 0;
}

static long initAo(aoRecord *pao)
{
    devInt32Pvt *pPvt;
    asynStatus status;
    epicsInt32 value;

    status = initCommon((dbCommon *)pao,&pao->out, processCallbackAo);
    if (status != asynSuccess) return 0;
    pPvt = pao->dpvt;
    pasynInt32SyncIO->getBoundsOnce(pPvt->portName,pPvt->addr,
                                &pPvt->deviceLow, &pPvt->deviceHigh);
    convertAo(pao, 1);
    /* Read the current value from the device */
    status = pasynInt32SyncIO->readOnce(pPvt->portName,pPvt->addr,
                      &value, pPvt->pasynUser->timeout);
    if (status == asynSuccess) {
        pao->rval = value;
        return 0;
    }
    return 2; /* Do not convert */
}

static long processAo(aoRecord *pr)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pr->dpvt;
    int status;

    if(pr->pact == 0) {
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) {
             pr->pact = 1;
             return 0;
        }
        if(status != asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynInt32::processCommon, error queuing request %s\n",
                pr->name,pPvt->pasynUser->errorMessage);
        }
    }
    return 0;
}

static void processCallbackAo(asynUser *pasynUser)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pasynUser->userPvt;
    dbCommon *pr = pPvt->pr;
    aoRecord *pao = (aoRecord *)pPvt->pr;
    int status=asynSuccess;

    status = pPvt->pint32->write(pPvt->int32Pvt, pPvt->pasynUser,pao->rval);
    if(status == asynSuccess) {
        pr->udf=0;
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
            "%s devAsynIn432 processAoInt32 rval %d\n",pr->name,pao->rval);
    } else {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "%s devAsynIn432 processAoInt32 error %s\n",
           pr->name, pasynUser->errorMessage);
       recGblSetSevr(pr, WRITE_ALARM, INVALID_ALARM);
    }
    if(pr->pact) callbackRequestProcessCallback(&pPvt->callback,pr->prio,pr);
}

static long initLi(longinRecord *pr)
{
    devInt32Pvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pr,&pr->inp, processCallbackLi);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    pasynManager->canBlock(pPvt->pasynUser, &pPvt->canBlock);
    scanIoInit(&pPvt->ioScanPvt);
    status = pPvt->pint32->registerInterruptUser(pPvt->int32Pvt,pPvt->pasynUser,
        callbackInterrupt,pPvt,&pPvt->registrarPvt);
    if(status!=asynSuccess) {
        printf("%s devAsynInt32 registerInterruptUser failed %s\n",
            pr->name,pPvt->pasynUser->errorMessage);
    }
    return 0;
}

static long processLi(longinRecord *pr)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pr->dpvt;
    int status;

    if(pPvt->gotValue) {
        pPvt->gotValue = 0;
    } else {
        if(pr->pact == 0) {
            status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
            if((status==asynSuccess) && pPvt->canBlock) {
                 pr->pact = 1;
                 return 0;
            }
            if(status != asynSuccess) {
                asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                    "%s devAsynInt32::processCommon, error queuing request %s\n", 
                    pr->name,pPvt->pasynUser->errorMessage);
            }
        }
    }
    pr->val = pPvt->value;
    return 0;
}


static void processCallbackLi(asynUser *pasynUser)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pasynUser->userPvt;
    dbCommon *pr = (dbCommon *)pPvt->pr;
    int status=asynSuccess;

    status = pPvt->pint32->read(pPvt->int32Pvt, pPvt->pasynUser, &pPvt->value);
    if (status == asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
            "%s devAsynInt32::processLiInt32 val=%d\n", pr->name, pPvt->value);
        pr->udf=0;
    } else {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "%s devAsynInt32::processLiInt32 read error %s\n",
              pr->name, pasynUser->errorMessage);
        recGblSetSevr(pr, READ_ALARM, INVALID_ALARM);
    }
    if(pr->pact) callbackRequestProcessCallback(&pPvt->callback,pr->prio,pr);
}

static long initLo(longoutRecord *pr)
{
    devInt32Pvt *pPvt;
    asynStatus status;
    epicsInt32 value;

    status = initCommon((dbCommon *)pr,&pr->out, processCallbackLo);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    /* Read the current value from the device */
    status = pasynInt32SyncIO->readOnce(pPvt->portName,pPvt->addr,
                      &value, pPvt->pasynUser->timeout);
    if (status == asynSuccess) pr->val = value;
    return 0;
}

static long processLo(longoutRecord *pr)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pr->dpvt;
    int status;

    if(pr->pact == 0) {
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) {
             pr->pact = 1;
             return 0;
        }
        if(status != asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynInt32::processCommon, error queuing request %s\n",
                pr->name,pPvt->pasynUser->errorMessage);
        }
    }
    return 0;
}

static void processCallbackLo(asynUser *pasynUser)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pasynUser->userPvt;
    dbCommon *pr = pPvt->pr;
    longoutRecord *plo = (longoutRecord *)pPvt->pr;
    int status=asynSuccess;

    status = pPvt->pint32->write(pPvt->int32Pvt, pPvt->pasynUser,plo->val);
    if(status == asynSuccess) {
        pr->udf=0;
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
            "%s devAsynIn432 processAoInt32 val %d\n",pr->name,plo->val);
    } else {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "%s devAsynIn432 processAoInt32 error %s\n",
           pr->name, pasynUser->errorMessage);
       recGblSetSevr(pr, WRITE_ALARM, INVALID_ALARM);
    }
    if(pr->pact) callbackRequestProcessCallback(&pPvt->callback,pr->prio,pr);
}

static long initMbbi(mbbiRecord *pr)
{
    devInt32Pvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pr,&pr->inp, processCallbackMbbi);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    pasynManager->canBlock(pPvt->pasynUser, &pPvt->canBlock);
    if(pr->nobt == 0) pr->mask = 0xffffffff;
    pr->mask <<= pr->shft;
    scanIoInit(&pPvt->ioScanPvt);
    status = pPvt->pint32->registerInterruptUser(pPvt->int32Pvt,pPvt->pasynUser,
        callbackInterrupt,pPvt,&pPvt->registrarPvt);
    if(status!=asynSuccess) {
        printf("%s devAsynInt32 registerInterruptUser failed %s\n",
            pr->name,pPvt->pasynUser->errorMessage);
    }
    return 0;
}

static long processMbbi(mbbiRecord *pr)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pr->dpvt;
    int status;

    if(pPvt->gotValue) {
        pPvt->gotValue = 0;
    } else {
        if(pr->pact == 0) {
            status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
            if((status==asynSuccess) && pPvt->canBlock) {
                 pr->pact = 1;
                 return 0;
            }
            if(status != asynSuccess) {
                asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                    "%s devAsynInt32::processCommon, error queuing request %s\n", 
                    pr->name,pPvt->pasynUser->errorMessage);
            } 
        }
    }
    pr->rval = pPvt->value & pr->mask;
    return 0;
}

static void processCallbackMbbi(asynUser *pasynUser)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pasynUser->userPvt;
    mbbiRecord *pmbbi = (mbbiRecord *)pPvt->pr;
    dbCommon *pr = (dbCommon *)pPvt->pr;
    int status=asynSuccess;

    status = pPvt->pint32->read(pPvt->int32Pvt, pPvt->pasynUser, &pPvt->value);
    if (status == asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
            "%s devAsynInt32::processMbbiInt32 val=%d\n",
            pr->name, pPvt->value&pmbbi->mask);
        pr->udf=0;
    } else {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "%s devAsynInt32::processMbbiInt32 read error %s\n",
              pr->name, pasynUser->errorMessage);
        recGblSetSevr(pr, READ_ALARM, INVALID_ALARM);
    }
    if(pr->pact) callbackRequestProcessCallback(&pPvt->callback,pr->prio,pr);
}

static long initMbbo(mbboRecord *pr)
{
    devInt32Pvt *pPvt;
    asynStatus status;
    epicsInt32 value;

    status = initCommon((dbCommon *)pr,&pr->out, processCallbackMbbo);
    if (status != asynSuccess) return 0;
    pPvt = pr->dpvt;
    if(pr->nobt == 0) pr->mask = 0xffffffff;
    pr->mask <<= pr->shft;
    /* Read the current value from the device */
    status = pasynInt32SyncIO->readOnce(pPvt->portName,pPvt->addr,
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

    if(pr->pact == 0) {
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) {
             pr->pact = 1;
             return 0;
        }
        if(status != asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynInt32::processCommon, error queuing request %s\n",
                pr->name,pPvt->pasynUser->errorMessage);
        }
    }
    return 0;
}

static void processCallbackMbbo(asynUser *pasynUser)
{
    devInt32Pvt *pPvt = (devInt32Pvt *)pasynUser->userPvt;
    dbCommon *pr = pPvt->pr;
    mbboRecord *pmbbo = (mbboRecord *)pPvt->pr;
    int status=asynSuccess;

    status = pPvt->pint32->write(pPvt->int32Pvt, pPvt->pasynUser,pmbbo->rval);
    if(status == asynSuccess) {
        pr->udf=0;
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
            "%s devAsynIn432 processAoInt32 rval %d\n",pr->name,pmbbo->rval);
    } else {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "%s devAsynIn432 processAoInt32 error %s\n",
           pr->name, pasynUser->errorMessage);
       recGblSetSevr(pr, WRITE_ALARM, INVALID_ALARM);
    }
    if(pr->pact) callbackRequestProcessCallback(&pPvt->callback,pr->prio,pr);
}
