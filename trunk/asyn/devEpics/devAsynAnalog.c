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

    This file provides device support for ai, ao, mbbi, and mbbo records
    asynAoInt32:
        Analog output, writes from rval, supports LINEAR conversion
        Uses asynInt32 interface
    asynMbboInt32:
        Analog output, writes from rval, supports BITS and MASK fields
        Uses asynInt32 interface
    asynAoFloat64:
        Analog output, writes from val, no conversion
        Uses asynFloat64 interface
    asynAiInt32:
        Analog input, reads into rval, supports LINEAR conversion
        Uses asynInt32 interface
    asynAiInt32Average: 
        Analog input, averages into rval, supports LINEAR conversion
        Uses asynInt32 and asynInt32Callback interfaces
        Each callback adds to an accumulator for averaging
        Processing the record resets the accumulator
    asynAiInt32Interrupt:
        Analog input, reads into rval, supports LINEAR conversion
        Uses asynInt32 and asynInt32Callback interfaces
        If record scan is I/O interrupt then record will process on 
        each callback
    asynMbbiInt32:
        Analog input, reads into rval, supports BITS and MASK fields
        Uses asynInt32 interface
    asynAiFloat64:
        Analog input, reads into val, no conversion
        Uses asynFloat64 interface
    asynAiFloat64Average:
        Analog input, averages into val, no conversion
        Uses asynFloat64Callback interface
        Each callback adds to an accumulator for averaging
        Processing the record resets the accumulator
    asynAiFloat64Interrupt:
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
#include <mbbiRecord.h>
#include <mbboRecord.h>
#include <recSup.h>
#include <devSup.h>

#include "asynDriver.h"
#include "asynDrvUser.h"
#include "asynInt32.h"
#include "asynInt32SyncIO.h"
#include "asynInt32Callback.h"
#include "asynFloat64.h"
#include "asynFloat64Callback.h"
#include "asynEpicsUtils.h"

typedef enum {
    typeAiInt32,
    typeAiInt32Average,
    typeAiInt32Interrupt,
    typeAiFloat64,
    typeAiFloat64Average,
    typeAiFloat64Interrupt,
    typeAoInt32,
    typeAoFloat64,
    typeMbbiInt32,
    typeMbboInt32
}asynAnalogDevType;

typedef struct devAsynAnalogPvt{
    dbCommon          *pr;
    asynUser          *pasynUser;
    asynInt32         *pint32;
    void              *int32Pvt;
    asynInt32Callback *pint32Callback;
    void              *int32CallbackPvt;
    asynFloat64       *pfloat64;
    void              *float64Pvt;
    asynFloat64Callback *pfloat64Callback;
    void              *float64CallbackPvt;
    int               canBlock;
    asynAnalogDevType devType;
    epicsInt32        deviceLow;
    epicsInt32        deviceHigh;
    epicsMutexId      mutexId;
    double            sum;
    int               numAverage;
    IOSCANPVT         ioScanPvt;
    int               ioStatus;
    char              *portName;
    char              *userParam;
    int               addr;
}devAsynAnalogPvt;

static long initCommon(dbCommon *pr, DBLINK *plink, userCallback callback,
                       asynAnalogDevType devType);
static long processCommon(dbCommon *pr);
static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt);
static long initAoInt32(aoRecord *pai);
static long initAoFloat64(aoRecord *pai);
static long initAiInt32(aiRecord *pai);
static long initAiFloat64(aiRecord *pai);
static long initAiInt32Average(aiRecord *pai);
static long initAiFloat64Average(aiRecord *pai);
static long initAiInt32Interrupt(aiRecord *pai);
static long initAiFloat64Interrupt(aiRecord *pai);
static long initMbboInt32(mbboRecord *pmbbo);
static long initMbbiInt32(mbbiRecord *pmbbi);
/* processCommon callbacks */
static void callbackOutput(asynUser *pasynUser);
static void callbackInput(asynUser *pasynUser);
static void callbackAiAverage(asynUser *pasynUser);
static void callbackAiInterrupt(asynUser *pasynUser);
/* driver callbacks */
static void callbackInt32Average(void *pvt, epicsInt32 data);
static void callbackInt32Interrupt(void *pvt, epicsInt32 data);
static void callbackFloat64Average(void *pvt, epicsFloat64 data);
static void callbackFloat64Interrupt(void *pvt, epicsFloat64 data);
/* compute eslo and eoff for linear conversions*/
static long convertAo(aoRecord *pai, int pass);
static long convertAi(aiRecord *pai, int pass);

typedef struct analogDset { /* analog  dset */
    long          number;
    DEVSUPFUN     dev_report;
    DEVSUPFUN     init;
    DEVSUPFUN     init_record; /*returns: (0,2)=>(success,success no convert)*/
    DEVSUPFUN     get_ioint_info;
    DEVSUPFUN     processCommon;/*(0)=>(success ) */
    DEVSUPFUN     special_linconv;
} analogDset;

analogDset asynAoInt32 =            {6, 0, 0, initAoInt32, 0, 
                                        processCommon, convertAo};
analogDset asynAoFloat64 =          {6, 0, 0, initAoFloat64, 0, 
                                        processCommon, convertAo};
analogDset asynAiInt32 =            {6, 0, 0, initAiInt32, 0, 
                                        processCommon, convertAi};
analogDset asynAiInt32Average =     {6, 0, 0, initAiInt32Average, 0, 
                                        processCommon, convertAi};
analogDset asynAiInt32Interrupt =   {6, 0, 0, initAiInt32Interrupt, 
                                        getIoIntInfo, processCommon, convertAi};
analogDset asynAiFloat64 =          {6, 0, 0, initAiFloat64, 0, 
                                        processCommon, convertAi};
analogDset asynAiFloat64Average =   {6, 0, 0, initAiFloat64Average, 0, 
                                        processCommon, convertAi};
analogDset asynAiFloat64Interrupt = {6, 0, 0, initAiFloat64Interrupt, 
                                        getIoIntInfo, processCommon, convertAi};
analogDset asynMbboInt32 =          {5, 0, 0, initMbboInt32, 0, 
                                        processCommon};
analogDset asynMbbiInt32 =          {5, 0, 0, initMbbiInt32, 0, 
                                        processCommon};

epicsExportAddress(dset, asynAoInt32);
epicsExportAddress(dset, asynAoFloat64);
epicsExportAddress(dset, asynAiInt32);
epicsExportAddress(dset, asynAiInt32Average);
epicsExportAddress(dset, asynAiInt32Interrupt);
epicsExportAddress(dset, asynAiFloat64);
epicsExportAddress(dset, asynAiFloat64Average);
epicsExportAddress(dset, asynAiFloat64Interrupt);
epicsExportAddress(dset, asynMbboInt32);
epicsExportAddress(dset, asynMbbiInt32);


static long initCommon(dbCommon *pr, DBLINK *plink, userCallback callback,
                       asynAnalogDevType devType)
{
    devAsynAnalogPvt *pPvt;
    char *itype;
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
                &pPvt->portName, &pPvt->addr, &pPvt->userParam);
    if (status != asynSuccess) {
        printf("devAsynAnalog::initCommon, %s error in link %s\n",
                     pr->name, pasynUser->errorMessage);
        goto bad;
    }

    /* Connect to device */
    status = pasynManager->connectDevice(pasynUser, pPvt->portName, pPvt->addr);
    if (status != asynSuccess) {
        printf("devAsynAnalog::initCommon, %s connectDevice failed %s\n",
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
            printf("devAsynAnalog::initCommon, %s drvUserCreate failed %s\n",
                     pr->name, pasynUser->errorMessage);
            goto bad;
        }
    }
    /* Get the appropriate interfaces */
    switch(devType) {
    case typeAiInt32Average:
    case typeAiInt32Interrupt:
        itype = asynInt32CallbackType;
        pasynInterface = pasynManager->findInterface(pasynUser, itype, 1);
        if (!pasynInterface) {
            printf("devAsynAnalog::initCommon, %s find %s interface failed\n",
                         pr->name, itype);
            goto bad;
        }
        pPvt->pint32Callback = pasynInterface->pinterface;
        pPvt->int32CallbackPvt = pasynInterface->drvPvt;
        /* No break.  These need asynInt32 */
    case typeAoInt32:
    case typeAiInt32:
    case typeMbboInt32:
    case typeMbbiInt32:
        itype = asynInt32Type;
        pasynInterface = pasynManager->findInterface(pasynUser, itype, 1);
        if (!pasynInterface) {
            printf("devAsynAnalog::initCommon, %s find %s interface failed\n",
                         pr->name, itype);
            goto bad;
        }
        pPvt->pint32 = pasynInterface->pinterface;
        pPvt->int32Pvt = pasynInterface->drvPvt;
        break;
    case typeAiFloat64Average:
    case typeAiFloat64Interrupt:
        itype = asynFloat64CallbackType;
        pasynInterface = pasynManager->findInterface(pasynUser, itype, 1);
        if (!pasynInterface) {
            printf("devAsynAnalog::initCommon, %s find %s interface failed\n",
                         pr->name, itype);
            goto bad;
        }
        pPvt->pfloat64Callback = pasynInterface->pinterface;
        pPvt->float64CallbackPvt = pasynInterface->drvPvt;
        /* No break.  These need asynFloat64 */
    case typeAoFloat64:
    case typeAiFloat64:
        itype = asynFloat64Type;
        pasynInterface = pasynManager->findInterface(pasynUser, itype, 1);
        if (!pasynInterface) {
            printf("devAsynAnalog::initCommon, %s find %s interface failed\n",
                         pr->name, itype);
            goto bad;
        }
        pPvt->pfloat64 = pasynInterface->pinterface;
        pPvt->float64Pvt = pasynInterface->drvPvt;
        break;
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
    devAsynAnalogPvt *pPvt;
    asynStatus status;
    epicsInt32 value;

    status = initCommon((dbCommon *)pao, (DBLINK *)&pao->out, callbackOutput, 
                        typeAoInt32);
    if (status == asynSuccess) {
        pPvt = pao->dpvt;
        pasynInt32SyncIO->getBoundsOnce(pPvt->portName,pPvt->addr,
                                &pPvt->deviceLow, &pPvt->deviceHigh);
        convertAo(pao, 1);
        /* Read the current value from the device */
        status = pasynInt32SyncIO->readOnce(pPvt->portName,pPvt->addr,
                          &value, pPvt->pasynUser->timeout);
        if (status == asynSuccess) {
            pao->rval = value;
            return(0);
        }
    }
    return(2); /* Do not convert */
}

static long initMbboInt32(mbboRecord *pmbbo)
{
    asynStatus status;

    status = initCommon((dbCommon *)pmbbo, (DBLINK *)&pmbbo->out, callbackOutput,
                        typeMbboInt32);
    if(pmbbo->nobt == 0) pmbbo->mask = 0xffffffff;
    pmbbo->mask <<= pmbbo->shft;
    /* don't convert */
    return(2);
}

static long initAoFloat64(aoRecord *pao)
{
    asynStatus status;

    status = initCommon((dbCommon *)pao, (DBLINK *)&pao->out, callbackOutput,
                        typeAoFloat64);
    return(2); /* Do not convert */
}

static long initAiInt32(aiRecord *pai)
{
    devAsynAnalogPvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pai, (DBLINK *)&pai->inp, callbackInput, 
                        typeAiInt32);
    if (status == asynSuccess) {
        pPvt = pai->dpvt;
        pasynInt32SyncIO->getBoundsOnce(pPvt->portName,pPvt->addr,
                                &pPvt->deviceLow, &pPvt->deviceHigh);
        /* set linear conversion slope */
        convertAi(pai, 1);
    }
    return(0);
}

static long initMbbiInt32(mbbiRecord *pmbbi)
{
    initCommon((dbCommon *)pmbbi, &pmbbi->inp, callbackInput, typeMbbiInt32);
    if(pmbbi->nobt == 0) pmbbi->mask = 0xffffffff;
    pmbbi->mask <<= pmbbi->shft;
    return(0);
}

static long initAiInt32Average(aiRecord *pai)
{
    devAsynAnalogPvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pai, (DBLINK *)&pai->inp, 
                        callbackAiAverage, typeAiInt32Average);
    if (status == asynSuccess) {
        pPvt = pai->dpvt;
        pPvt->pint32Callback->registerCallback(pPvt->int32CallbackPvt, 
                                                pPvt->pasynUser, 
                                                callbackInt32Average, 
                                                pPvt);
        pasynInt32SyncIO->getBoundsOnce(pPvt->portName,pPvt->addr,
                                &pPvt->deviceLow, &pPvt->deviceHigh);
        /* set linear conversion slope */
        convertAi(pai, 1);
    }
    return(0);
}

static long initAiInt32Interrupt(aiRecord *pai)
{
    devAsynAnalogPvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pai, (DBLINK *)&pai->inp, callbackAiInterrupt,
                        typeAiInt32Interrupt);
    if (status == asynSuccess) {
        pPvt = pai->dpvt;
        pPvt->pint32Callback->registerCallback(pPvt->int32CallbackPvt, 
                                                pPvt->pasynUser,
                                                callbackInt32Interrupt, 
                                                pPvt);
        pasynInt32SyncIO->getBoundsOnce(pPvt->portName,pPvt->addr,
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

    status = initCommon((dbCommon *)pai, (DBLINK *)&pai->inp, callbackInput,
                        typeAiFloat64);
    return(0);
}

static long initAiFloat64Average(aiRecord *pai)
{
    devAsynAnalogPvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pai, (DBLINK *)&pai->inp, callbackAiAverage,
                        typeAiFloat64Average);
    if (status == asynSuccess) {
        pPvt = pai->dpvt;
        pPvt->pfloat64Callback->registerCallback(pPvt->float64CallbackPvt, 
                                                  pPvt->pasynUser,
                                                  callbackFloat64Average, 
                                                  pPvt);
    }
    return(0);
}

static long initAiFloat64Interrupt(aiRecord *pai)
{
    devAsynAnalogPvt *pPvt;
    asynStatus status;

    status = initCommon((dbCommon *)pai, (DBLINK *)&pai->inp, callbackAiInterrupt,
                        typeAiFloat64Interrupt);
    if (status == asynSuccess) {
        pPvt = pai->dpvt;
        pPvt->pfloat64Callback->registerCallback(pPvt->float64CallbackPvt, 
                                                  pPvt->pasynUser,
                                                  callbackFloat64Interrupt, 
                                                  pPvt);
        scanIoInit(&pPvt->ioScanPvt);
     }
    return(0);
}

static long processCommon(dbCommon *pr)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)pr->dpvt;
    int status;

    if (pr->pact == 0) {   /* This is an initial call from record */
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if (status != asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynAnalog::processCommon, error queuing request %s\n", 
                pr->name,pPvt->pasynUser->errorMessage);
            pPvt->ioStatus = -1;
        }else {
            if (pPvt->canBlock) pr->pact = 1;
            pPvt->ioStatus = 0;
        }
        if (pr->pact) return(0);
    }
    /* This is either a second call from record or the device is synchronous.
     * In either case the I/O is complete.
     * Return the appropriate status */
    if (pPvt->ioStatus != 0) return(pPvt->ioStatus);
    switch (pPvt->devType) {
    case typeAiFloat64:
    case typeAiFloat64Average:
    case typeAiFloat64Interrupt:
        return(2);  /* Do not convert */
    default:
        return(0);
    }
}

static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)pr->dpvt;

    *iopvt = pPvt->ioScanPvt;
    return 0;
}

static void callbackOutput(asynUser *pasynUser)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)pasynUser->userPvt;
    dbCommon *pr = pPvt->pr;
    aoRecord *pao = (aoRecord *)pPvt->pr;
    mbboRecord *pmbbo = (mbboRecord *)pPvt->pr;
    rset *prset = (rset *)pr->rset;
    int status;
    epicsInt32 ivalue=0;
    epicsFloat64 dvalue=0;

    if (pPvt->devType == typeAoInt32) {
        ivalue = pao->rval;
        status = pPvt->pint32->write(pPvt->int32Pvt, pPvt->pasynUser, ivalue);
    } else if (pPvt->devType == typeMbboInt32) {
        ivalue = pmbbo->rval & pmbbo->mask;
        status = pPvt->pint32->write(pPvt->int32Pvt, pPvt->pasynUser, ivalue);
    } else {
        dvalue = pao->val;
        status = pPvt->pfloat64->write(pPvt->float64Pvt, pPvt->pasynUser, pao->val);
    }
    asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
              "%s devAsynAnalog::callbackOutput devType=%d, ivalue=%d, dvalue=%f\n",
              pr->name, pPvt->devType, ivalue, dvalue);
    if (status == asynSuccess) {
        pPvt->ioStatus = 0;
        pr->udf=0;
    } else {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "%s devAsynAnalog::callbackOut write error %s\n",
              pr->name, pasynUser->errorMessage);
        pPvt->ioStatus = -1;
        recGblSetSevr(pr, WRITE_ALARM, INVALID_ALARM);
    }
    if (pr->pact) {
        dbScanLock(pr);
        prset->process(pr);
        dbScanUnlock(pr);
    }
}

static void callbackInput(asynUser *pasynUser)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)pasynUser->userPvt;
    dbCommon *pr = pPvt->pr;
    aiRecord *pai = (aiRecord *)pPvt->pr;
    mbbiRecord *pmbbi = (mbbiRecord *)pPvt->pr;
    rset *prset = (rset *)pai->rset;
    int status;
    epicsInt32 ivalue=0;
    epicsFloat64 dvalue=0.;

    if (pPvt->devType == typeAiInt32) {
        status = pPvt->pint32->read(pPvt->int32Pvt, pPvt->pasynUser, &pai->rval);
        ivalue = pai->rval;
    } else if (pPvt->devType == typeMbbiInt32) {
        status = pPvt->pint32->read(pPvt->int32Pvt, pPvt->pasynUser, 
                                    (epicsInt32 *)&pmbbi->rval);
        pmbbi->rval = pmbbi->rval & pmbbi->mask;
        ivalue = pmbbi->rval;
    } else {
        status = pPvt->pfloat64->read(pPvt->float64Pvt, pPvt->pasynUser, &pai->val);
        dvalue = pai->val;
    }
    asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
              "%s devAsynAnalog::callbackInput devType=%d, ivalue=%d, dvalue=%f\n",
              pr->name, pPvt->devType, ivalue, dvalue);
    if (status == asynSuccess) {
        pPvt->ioStatus = 0;
        pr->udf=0;
    } else {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "%s devAsynAnalog::callbackInput read error %s\n",
              pr->name, pasynUser->errorMessage);
        pPvt->ioStatus = -1;
        recGblSetSevr(pr, READ_ALARM, INVALID_ALARM);
    }
    if (pr->pact) {
        dbScanLock(pr);
        prset->process(pr);
        dbScanUnlock(pr);
    }
}


static void callbackAiAverage(asynUser *pasynUser)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)pasynUser->userPvt;
    aiRecord *pai = (aiRecord *)pPvt->pr;
    rset *prset = (rset *)pai->rset;

    epicsMutexLock(pPvt->mutexId);
    if (pPvt->numAverage == 0) pPvt->numAverage = 1;
    if (pPvt->devType == typeAiInt32Average) {
        double rval;
        rval = pPvt->sum/pPvt->numAverage;
        /*round result*/
        rval += (pPvt->sum>0.0) ? 0.5 : -0.5;
        pai->rval = rval;
    } else {
        pai->val = pPvt->sum/pPvt->numAverage;
    }
    pPvt->numAverage = 0;
    pPvt->sum = 0.;
    epicsMutexUnlock(pPvt->mutexId);
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "%s devAsynAnalog::callbackAiAverage rval=%d val=%f\n",
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

    epicsMutexLock(pPvt->mutexId);
    if (pPvt->devType == typeAiInt32Interrupt)
        pai->rval = pPvt->sum;
    else
        pai->val = pPvt->sum;
    epicsMutexUnlock(pPvt->mutexId);
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "%s devAsynAnalog::callbackAiInterrupt rval=%d val=%f\n",
              pai->name, pai->rval, pai->val);
    if (pai->pact) {
        dbScanLock((dbCommon *)pai);
        prset->process(pai);
        dbScanUnlock((dbCommon *)pai);
    }
}

static void callbackInt32Average(void *drvPvt, epicsInt32 value)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)drvPvt;
    dbCommon *pr = pPvt->pr;

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "%s devAsynAnalog::callbackInt32Average new value=%d\n",
              pr->name, value);
    pr->udf = 0;
    epicsMutexLock(pPvt->mutexId);
    pPvt->numAverage++;
    pPvt->sum += (double)value;
    epicsMutexUnlock(pPvt->mutexId);
}

static void callbackFloat64Average(void *drvPvt, epicsFloat64 value)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)drvPvt;
    dbCommon *pr = pPvt->pr;

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "%s devAsynAnalog::callbackFloat64Average new value=%f\n",
              pr->name, value);
    pr->udf = 0;
    epicsMutexLock(pPvt->mutexId);
    pPvt->numAverage++;
    pPvt->sum += value;
    epicsMutexUnlock(pPvt->mutexId);
}

static void callbackInt32Interrupt(void *drvPvt, epicsInt32 value)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)drvPvt;
    aiRecord *pai = (aiRecord *)pPvt->pr;
    dbCommon *pr = pPvt->pr;

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "%s devAsynAnalog::callbackInt32Interrupt new value=%d\n",
              pr->name, value);
    epicsMutexLock(pPvt->mutexId);
    pPvt->sum = value;
    epicsMutexUnlock(pPvt->mutexId);
    pai->udf = 0;
    scanIoRequest(pPvt->ioScanPvt);
}

static void callbackFloat64Interrupt(void *drvPvt, epicsFloat64 value)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)drvPvt;
    aiRecord *pai = (aiRecord *)pPvt->pr;
    dbCommon *pr = pPvt->pr;

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "%s devAsynAnalog::callbackFloat64Interrupt new value=%f\n",
              pr->name, value);
    epicsMutexLock(pPvt->mutexId);
    pPvt->sum = value;
    epicsMutexUnlock(pPvt->mutexId);
    pai->udf = 0;
    scanIoRequest(pPvt->ioScanPvt);
}

static long convertAo(aoRecord *precord, int pass)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)precord->dpvt;
    double eguf,egul,deviceHigh,deviceLow;

    if (pass==0) return(0);
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

static long convertAi(aiRecord *precord, int pass)
{
    devAsynAnalogPvt *pPvt = (devAsynAnalogPvt *)precord->dpvt;
    double eguf,egul,deviceHigh,deviceLow;

    if (pass==0) return(0);
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
