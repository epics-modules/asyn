/* devAsynAnalog.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
    Author:  Mark Rivers
    31-July-2004

    This file provides device support for ai and ao records
    devAoAsynInt32:
        Analog output, writes from rval, supports LINEAR conversion
        Uses asynInt32 interface
    devAoAsynFloat64:
        Analog output, writes from val, no conversion
        Uses asynFloat64 interface
    devAiAsynInt32:
        Analog input, reads into rval, supports LINEAR conversion
        Uses asynInt32 interface
    devAiAsynInt32Average: 
        Analog input, averages into rval, supports LINEAR conversion
        Uses asynInt32Callback interface
        Each callback adds to an accumulator for averaging
        Processing the record resets the accumulator
    devAiAsynInt32Interrupt:
        Analog input, reads into rval, supports LINEAR conversion
        Uses asynInt32Callback interface
        If record scan is I/O interrupt then record will process on 
        each callback
    devAiAsynFloat64:
        Analog input, reads into val, no conversion
        Uses asynFloat64 interface
    devAiAsynFloat64Average:
        Analog input, averages into val, no conversion
        Uses asynFloat64Callback interface
        Each callback adds to an accumulator for averaging
        Processing the record resets the accumulator
    devAiAsynFloat64Interrupt:
        Analog input, reads into val, no conversion
        Uses asynFloat64Callback interface
        If record scan is I/O interrupt then record will process on 
        each callback
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
#include <aoRecord.h>
#include <aiRecord.h>
#include <recSup.h>
#include <devSup.h>

#include "asynDriver.h"
#include "asynInt32.h"
#include "asynInt32Callback.h"
#include "asynFloat64.h"
#include "asynFloat64Callback.h"
#include "asynEpicsUtils.h"

typedef enum {
    typeAiInt32,
    typeAoInt32,
    typeAiInt32Average,
    typeAiInt32Interrupt,
    typeAiFloat64,
    typeAoFloat64,
    typeAiFloat64Average,
    typeAiFloat64Interrupt
} asynAnalogDevType;

typedef struct {
    dbCommon          *pr;
    asynUser          *pasynUser;
    void              *dataInterface;
    void              *dataPvt;
    void              *callbackInterface;
    void              *callbackPvt;
    int               canBlock;
    asynAnalogDevType devType;
    epicsInt32        deviceLow;
    epicsInt32        deviceHigh;
    epicsMutexId      mutexId;
    double            sum;
    int               numAverage;
    IOSCANPVT         ioScanPvt;
} devAsynAnalogPvt;

static long initCommon(dbCommon *pr, DBLINK *plink, userCallback callback,
                       char *dataInterfaceType, char *callbackInterfaceType,
                       asynAnalogDevType devType);
static long queueRequest(dbCommon *pr);
static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt);
static long initAoInt32(aoRecord *pai);
static long initAoFloat64(aoRecord *pai);
static long initAiInt32(aiRecord *pai);
static long initAiFloat64(aiRecord *pai);
static long initAiInt32Average(aiRecord *pai);
static long initAiFloat64Average(aiRecord *pai);
static long initAiInt32Interrupt(aiRecord *pai);
static long initAiFloat64Interrupt(aiRecord *pai);
static void callbackAo(asynUser *pasynUser);
static void callbackAi(asynUser *pasynUser);
static void callbackAiAverage(asynUser *pasynUser);
static void callbackAiInterrupt(asynUser *pasynUser);
static void dataCallbackInt32Average(void *pvt, epicsInt32 data);
static void dataCallbackInt32Interrupt(void *pvt, epicsInt32 data);
static void dataCallbackFloat64Average(void *pvt, epicsFloat64 data);
static void dataCallbackFloat64Interrupt(void *pvt, epicsFloat64 data);
static long convertAo(aoRecord *pai, int pass);
static long convertAi(aiRecord *pai, int pass);

typedef struct analogDset { /* analog  dset */
    long          number;
    DEVSUPFUN     dev_report;
    DEVSUPFUN     init;
    DEVSUPFUN     init_record; /*returns: (0,2)=>(success,success no convert)*/
    DEVSUPFUN     get_ioint_info;
    DEVSUPFUN     queueRequest;/*(0)=>(success ) */
    DEVSUPFUN     special_linconv;
} analogDset;

analogDset devAoAsynInt32 =            {6, 0, 0, initAoInt32, 0, 
                                        queueRequest, convertAo};
analogDset devAoAsynFloat64 =          {6, 0, 0, initAoFloat64, 0, 
                                        queueRequest, convertAo};
analogDset devAiAsynInt32 =            {6, 0, 0, initAiInt32, 0, 
                                        queueRequest, convertAi};
analogDset devAiAsynInt32Average =     {6, 0, 0, initAiInt32Average, 0, 
                                        queueRequest, convertAi};
analogDset devAiAsynInt32Interrupt =   {6, 0, 0, initAiInt32Interrupt, 
                                        getIoIntInfo, queueRequest, convertAi};
analogDset devAiAsynFloat64 =          {6, 0, 0, initAiFloat64, 0, 
                                        queueRequest, convertAi};
analogDset devAiAsynFloat64Average =   {6, 0, 0, initAiFloat64Average, 0, 
                                        queueRequest, convertAi};
analogDset devAiAsynFloat64Interrupt = {6, 0, 0, initAiFloat64Interrupt, 
                                        getIoIntInfo, queueRequest, convertAi};

epicsExportAddress(dset, devAoAsynInt32);
epicsExportAddress(dset, devAoAsynFloat64);
epicsExportAddress(dset, devAiAsynInt32);
epicsExportAddress(dset, devAiAsynInt32Average);
epicsExportAddress(dset, devAiAsynInt32Interrupt);
epicsExportAddress(dset, devAiAsynFloat64);
epicsExportAddress(dset, devAiAsynFloat64Average);
epicsExportAddress(dset, devAiAsynFloat64Interrupt);


static long initCommon(dbCommon *pr, DBLINK *plink, userCallback callback,
                       char *dataInterfaceType, char *callbackInterfaceType,
                       asynAnalogDevType devType)
{
    devAsynAnalogPvt *pPvt;
    char *port, *userParam;
    int addr;
    asynStatus status;
    asynUser *pasynUser;
    asynInterface *pasynInterface;

    pPvt = callocMustSucceed(1, sizeof(*pPvt), "devAsynAnalog::initCommon");
    pr->dpvt = pPvt;
    pPvt->pr = pr;

    /* Create asynUser */
    pasynUser = pasynManager->createAsynUser(callback, 0);
    pasynUser->userPvt = pPvt;
    pPvt->pasynUser = pasynUser;
    pPvt->mutexId = epicsMutexCreate();
    pPvt->devType = devType;

    /* Parse the link to get addr and port */
    status = pasynEpicsUtils->parseLink(pasynUser, plink, 
                                        &port, &addr, &userParam);
    if (status != asynSuccess) {
        errlogPrintf("devAsynAnalog::initCommon, error in link %s\n",
                     pasynUser->errorMessage);
        goto bad;
    }

    /* Connect to device */
    status = pasynManager->connectDevice(pasynUser, port, addr);
    if (status != asynSuccess) {
        errlogPrintf("devAsynAnalog::initCommon, connectDevice failed\n");
        goto bad;
    }

    /* Get the appropriate interfaces */
    pasynInterface = pasynManager->findInterface(pasynUser, dataInterfaceType, 1);
    if (!pasynInterface) {
        errlogPrintf("devAsynAnalog::initCommon, find %s interface failed\n",
                     dataInterfaceType);
        goto bad;
    }
    pPvt->dataInterface = pasynInterface->pinterface;
    pPvt->dataPvt = pasynInterface->drvPvt;

    if (callbackInterfaceType) {
        pasynInterface = pasynManager->findInterface(pasynUser, 
                                                     callbackInterfaceType, 1);
        if (!pasynInterface) {
            errlogPrintf("devAsynAnalog::initCommon, find %s interface failed\n",
                     callbackInterfaceType);
            goto bad;
        }
        pPvt->callbackInterface = pasynInterface->pinterface;
        pPvt->callbackPvt = pasynInterface->drvPvt;
    }

    /* Determine if device can block */
    pasynManager->canBlock(pasynUser, &pPvt->canBlock);
    return(0);

bad:
   pr->pact=1;
   return(-1);
}


static long initAoInt32(aoRecord *pao)
{
    asynInt32 *pasynInt32;
    devAsynAnalogPvt *pPvt;
    asynStatus status;
    epicsInt32 value;

    status = initCommon((dbCommon *)pao, (DBLINK *)&pao->out, callbackAo, 
                        asynInt32Type, 0, typeAoInt32);
    if (status == asynSuccess) {
        pPvt = pao->dpvt;
        pasynInt32 = (asynInt32 *)pPvt->dataInterface;
        pasynInt32->getBounds(pPvt->dataPvt, pPvt->pasynUser,
                              &pPvt->deviceLow, &pPvt->deviceHigh);
        /* set linear conversion slope */
        convertAo(pao, 1);
        /* Read the current value from the device */
        status = pasynInt32->read(pPvt->dataPvt, pPvt->pasynUser, &value);
        if (status == asynSuccess) {
            pao->rval = value;
            return(0);
        }
    }
    return(2); /* Do not convert */
}

static long initAoFloat64(aoRecord *pao)
{
    asynStatus status;

    status = initCommon((dbCommon *)pao, (DBLINK *)&pao->out, callbackAo,
                        asynFloat64Type, 0, typeAoFloat64);
    return(2); /* Do not convert */
}

static long initAiInt32(aiRecord *pai)
{
    asynInt32 *pasynInt32;
    devAsynAnalogPvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pai, (DBLINK *)&pai->inp, callbackAi, 
                        asynInt32Type, 0, typeAiInt32);
    if (status == asynSuccess) {
        pPvt = pai->dpvt;
        pasynInt32 = (asynInt32 *)pPvt->dataInterface;
        pasynInt32->getBounds(pPvt->dataPvt, pPvt->pasynUser, 
                              &pPvt->deviceLow, &pPvt->deviceHigh);
        /* set linear conversion slope */
        convertAi(pai, 1);
    }
    return(0);
}

static long initAiInt32Average(aiRecord *pai)
{
    asynInt32 *pasynInt32;
    asynInt32Callback *pasynInt32Callback;
    devAsynAnalogPvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pai, (DBLINK *)&pai->inp, 
                        callbackAiAverage, asynInt32Type, asynInt32CallbackType,
                        typeAiInt32Average);
    if (status == asynSuccess) {
        pPvt = pai->dpvt;
        pasynInt32Callback = (asynInt32Callback *)pPvt->callbackInterface;
        pasynInt32Callback->registerCallbacks(pPvt->callbackPvt, 
                                              pPvt->pasynUser, 
                                              dataCallbackInt32Average, 
                                              0, pPvt);
        pasynInt32 = (asynInt32 *)pPvt->dataInterface;
        pasynInt32->getBounds(pPvt->dataPvt, pPvt->pasynUser, 
                              &pPvt->deviceLow, &pPvt->deviceHigh);
        /* set linear conversion slope */
        convertAi(pai, 1);
    }
    return(0);
}

static long initAiInt32Interrupt(aiRecord *pai)
{
    asynInt32 *pasynInt32;
    asynInt32Callback *pasynInt32Callback;
    devAsynAnalogPvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pai, (DBLINK *)&pai->inp, 
                        callbackAiInterrupt, asynInt32Type, 
                        asynInt32CallbackType, typeAiInt32Interrupt);
    if (status == asynSuccess) {
        pPvt = pai->dpvt;
        pasynInt32Callback = (asynInt32Callback *)pPvt->callbackInterface;
        pasynInt32Callback->registerCallbacks(pPvt->callbackPvt, 
                                              pPvt->pasynUser,
                                              dataCallbackInt32Interrupt, 
                                              0, pPvt);
        pasynInt32 = (asynInt32 *)pPvt->dataInterface;
        pasynInt32->getBounds(pPvt->dataPvt, pPvt->pasynUser, 
                              &pPvt->deviceLow, &pPvt->deviceHigh);
        /* set linear conversion slope */
        convertAi(pai, 1);
        scanIoInit(&pPvt->ioScanPvt);
     }
    return(0);
}

static long initAiFloat64(aiRecord *pai)
{
    asynStatus status;

    status = initCommon((dbCommon *)pai, (DBLINK *)&pai->inp, callbackAi,
                        asynFloat64Type, 0, typeAiFloat64);
    return(0);
}

static long initAiFloat64Average(aiRecord *pai)
{
    asynFloat64Callback *pasynFloat64Callback;
    devAsynAnalogPvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pai, (DBLINK *)&pai->inp,
                        callbackAiAverage, asynFloat64Type,
                        asynFloat64CallbackType, typeAiFloat64Average);
    if (status == asynSuccess) {
        pPvt = pai->dpvt;
        pasynFloat64Callback = (asynFloat64Callback *)pPvt->callbackInterface;
        pasynFloat64Callback->registerCallbacks(pPvt->callbackPvt, 
                                                pPvt->pasynUser,
                                                dataCallbackFloat64Average, 
                                                0, pPvt);
    }
    return(0);
}

static long initAiFloat64Interrupt(aiRecord *pai)
{
    asynFloat64Callback *pasynFloat64Callback;
    devAsynAnalogPvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pai, (DBLINK *)&pai->inp,
                        callbackAiInterrupt, asynFloat64Type, 
                        asynFloat64CallbackType,
                        typeAiFloat64Interrupt);
    if (status == asynSuccess) {
        pPvt = pai->dpvt;
        pasynFloat64Callback = (asynFloat64Callback *)pPvt->callbackInterface;
        pasynFloat64Callback->registerCallbacks(pPvt->callbackPvt, 
                                                pPvt->pasynUser,
                                                dataCallbackFloat64Interrupt, 
                                                0, pPvt);
        scanIoInit(&pPvt->ioScanPvt);
     }
    return(0);
}

static long queueRequest(dbCommon *pr)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)pr->dpvt;
    int status;

    if (pr->pact) {
        /* This is second call from record after I/O is complete. Nothing
         * to do, return */
        switch (pPvt->devType) {
        case typeAiFloat64:
        case typeAiFloat64Average:
        case typeAiFloat64Interrupt:
            return(2);  /* Do not convert */
        default:
            return(0);
        }
    }
    if (pPvt->canBlock) pr->pact = 1;
    status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
    if (status != asynSuccess) {
        asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                  "devAsynAnalog::queueRequest, error queuing request %s\n", 
                  pPvt->pasynUser->errorMessage);
    }
    return status;
}

static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)pr->dpvt;

    *iopvt = pPvt->ioScanPvt;
    return 0;
}

static void callbackAo(asynUser *pasynUser)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)pasynUser->userPvt;
    aoRecord *pao = (aoRecord *)pPvt->pr;
    rset *prset = (rset *)pao->rset;
    asynInt32 *pasynInt32;
    asynFloat64 *pasynFloat64;
    int status;

    asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
              "devAsynAnalog::callbackAo %s devType=%d, rval=%d, val=%f\n",
              pao->name, pPvt->devType, pao->rval, pao->val);
    if (pPvt->devType == typeAoInt32) {
        pasynInt32 = (asynInt32 *)pPvt->dataInterface;
        status = pasynInt32->write(pPvt->dataPvt, pPvt->pasynUser, pao->rval);
    } else {
        pasynFloat64 = (asynFloat64 *)pPvt->dataInterface;
        status = pasynFloat64->write(pPvt->dataPvt, pPvt->pasynUser, pao->val);
    }
    if (status == 0)
        pao->udf=0;
    else
        recGblSetSevr(pao,WRITE_ALARM,INVALID_ALARM);
    if (pao->pact) {
        dbScanLock((dbCommon *)pao);
        prset->process(pao);
        dbScanUnlock((dbCommon *)pao);
    }
}

static void callbackAi(asynUser *pasynUser)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)pasynUser->userPvt;
    aiRecord *pai = (aiRecord *)pPvt->pr;
    rset *prset = (rset *)pai->rset;
    asynInt32 *pasynInt32;
    asynFloat64 *pasynFloat64;
    int status;

    if (pPvt->devType == typeAiInt32) {
        pasynInt32 = (asynInt32 *)pPvt->dataInterface;
        status = pasynInt32->read(pPvt->dataPvt, pPvt->pasynUser, &pai->rval);
    } else {
        pasynFloat64 = (asynFloat64 *)pPvt->dataInterface;
        status = pasynFloat64->read(pPvt->dataPvt, pPvt->pasynUser, &pai->val);
    }
    asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
              "devAsynAnalog::callbackAi %s devType=%d, rval=%d, val=%f\n",
              pai->name, pPvt->devType, pai->rval, pai->val);
    if (status == 0)
        pai->udf=0;
    else
        recGblSetSevr(pai,WRITE_ALARM,INVALID_ALARM);
    if (pai->pact) {
        dbScanLock((dbCommon *)pai);
        prset->process(pai);
        dbScanUnlock((dbCommon *)pai);
    }
}

static void callbackAiAverage(asynUser *pasynUser)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)pasynUser->userPvt;
    aiRecord *pai = (aiRecord *)pPvt->pr;
    rset *prset = (rset *)pai->rset;

    epicsMutexLock(pPvt->mutexId);
    if (pPvt->numAverage == 0) pPvt->numAverage = 1;
    if (pPvt->devType == typeAiInt32Average)
        pai->rval = pPvt->sum/pPvt->numAverage + 0.5;
    else
        pai->val = pPvt->sum/pPvt->numAverage;
    pPvt->numAverage = 0;
    pPvt->sum = 0.;
    epicsMutexUnlock(pPvt->mutexId);
    pai->udf=0;
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "devAsynAnalog::callbackAiAverage %s rval=%d val=%f\n",
              pai->name, pai->rval, pai->val);
    if (pai->pact) {
        dbScanLock((dbCommon *)pai);
        prset->process(pai);
        dbScanUnlock((dbCommon *)pai);
    }
}

static void callbackAiInterrupt(asynUser *pasynUser)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)pasynUser->userPvt;
    aiRecord *pai = (aiRecord *)pPvt->pr;
    rset *prset = (rset *)pai->rset;

    if (pPvt->devType == typeAiInt32Interrupt)
        pai->rval = pPvt->sum;
    else
        pai->val = pPvt->sum;
    pai->udf=0;
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "devAsynAnalog::callbackAiInterrupt %s rval=%d val=%f\n",
              pai->name, pai->rval, pai->val);
    if (pai->pact) {
        dbScanLock((dbCommon *)pai);
        prset->process(pai);
        dbScanUnlock((dbCommon *)pai);
    }
}

static void dataCallbackInt32Average(void *drvPvt, epicsInt32 value)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)drvPvt;
    dbCommon *pr = pPvt->pr;

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "devAsynAnalog::dataCallbackInt32Average %s new value=%d\n",
              pr->name, value);
    epicsMutexLock(pPvt->mutexId);
    pPvt->numAverage++;
    pPvt->sum += (double)value;
    epicsMutexUnlock(pPvt->mutexId);
}

static void dataCallbackFloat64Average(void *drvPvt, epicsFloat64 value)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)drvPvt;
    dbCommon *pr = pPvt->pr;

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "devAsynAnalog::dataCallbackFloat64Average %s new value=%f\n",
              pr->name, value);
    epicsMutexLock(pPvt->mutexId);
    pPvt->numAverage++;
    pPvt->sum += value;
    epicsMutexUnlock(pPvt->mutexId);
}

static void dataCallbackInt32Interrupt(void *drvPvt, epicsInt32 value)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)drvPvt;
    aiRecord *pai = (aiRecord *)pPvt->pr;
    dbCommon *pr = pPvt->pr;

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "devAsynAnalog::dataCallbackInt32Interrupt %s new value=%d\n",
              pr->name, value);

    pPvt->sum = value;
    pai->udf = 0;
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "devAsynAnalog::dataCallbackInt32Interrupt, new value=%d\n", 
              value);
    scanIoRequest(pPvt->ioScanPvt);
}

static void dataCallbackFloat64Interrupt(void *drvPvt, epicsFloat64 value)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)drvPvt;
    aiRecord *pai = (aiRecord *)pPvt->pr;
    dbCommon *pr = pPvt->pr;

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "devAsynAnalog::dataCallbackFloat64Interrupt %s new value=%f\n",
              pr->name, value);

    pPvt->sum = value;
    pai->udf = 0;
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "devAsynAnalog::dataCallbackFloat64Interrupt, new value=%d\n", 
              value);
    scanIoRequest(pPvt->ioScanPvt);
}

static long convertAo(aoRecord *pao, int pass)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)pao->dpvt;

    if (pass==0) return(0);
    /* set linear conversion slope */
    pao->eslo = (pao->eguf - pao->egul)/(pPvt->deviceHigh - pPvt->deviceLow);
    return 0;
}

static long convertAi(aiRecord *pai, int pass)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)pai->dpvt;

    if (pass==0) return(0);
    /* set linear conversion slope */
    pai->eslo = (pai->eguf - pai->egul)/(pPvt->deviceHigh - pPvt->deviceLow);
    return 0;
}

