/* devAsynInt32Array.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
    Author:  Geoff Savage
    19NOV2004
*/

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include <alarm.h>
#include <recGbl.h>
#include <dbAccess.h>
#include <callback.h>
#include <dbDefs.h>
#include <link.h>
#include <errlog.h>
#include <epicsMutex.h>
#include <cantProceed.h>
#include <dbCommon.h>
#include <dbScan.h>
#include <waveformRecord.h>
#include <recSup.h>
#include <devSup.h>
#include <menuFtype.h>

#include <epicsExport.h>
#include "asynDriver.h"
#include "asynDrvUser.h"
#include "asynInt32Array.h"
#include "asynEpicsUtils.h"
#include <epicsExport.h>

typedef struct devAsynWfPvt{
    dbCommon        *pr;
    asynUser        *pasynUser;
    asynInt32Array  *pint32Array;
    void            *int32ArrayPvt;
    void            *registrarPvt;
    int             canBlock;
    CALLBACK        callback;
    IOSCANPVT       ioScanPvt;
    asynStatus      status;
    int             gotValue; /*For interruptCallbackInput */
    int             nord;
    interruptCallbackInt32Array interruptCallback;
    char            *portName;
    char            *userParam;
    int             addr;
} devAsynWfPvt;

static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt);
static long initCommon(dbCommon *pr, DBLINK *plink, 
    userCallback callback, interruptCallbackInt32Array interruptCallback);
static long processCommon(dbCommon *pr);
static long initWfInt32Array(waveformRecord *pwf);
static long initWfOutInt32Array(waveformRecord *pwf);
/* processCommon callbacks */
static void callbackWf(asynUser *pasynUser);
static void callbackWfOut(asynUser *pasynUser);
static void interruptCallbackInput(void *drvPvt, asynUser *pasynUser, 
                epicsInt32 *value, size_t len);
static void interruptCallbackOutput(void *drvPvt, asynUser *pasynUser, 
                epicsInt32 *value, size_t len);

typedef struct analogDset { /* analog  dset */
    long        number;
    DEVSUPFUN     dev_report;
    DEVSUPFUN     init;
    DEVSUPFUN     init_record;
    DEVSUPFUN     get_ioint_info;
    DEVSUPFUN     processCommon;/*(0)=>(success ) */
    DEVSUPFUN     special_linconv;
} analogDset;

analogDset asynInt32ArrayWfIn =
    {6, 0, 0, initWfInt32Array,    getIoIntInfo, processCommon, 0};
analogDset asynInt32ArrayWfOut =
    {6, 0, 0, initWfOutInt32Array, getIoIntInfo, processCommon, 0};

epicsExportAddress(dset, asynInt32ArrayWfIn);
epicsExportAddress(dset, asynInt32ArrayWfOut);

static long initCommon(dbCommon *pr, DBLINK *plink, 
    userCallback callback, interruptCallbackInt32Array interruptCallback)
{
    waveformRecord *pwf = (waveformRecord *)pr;
    devAsynWfPvt *pPvt;
    asynStatus status;
    asynUser *pasynUser;
    asynInterface *pasynInterface;

    pPvt = callocMustSucceed(1, sizeof(*pPvt), "devAsynWf::initCommon");
    pr->dpvt = pPvt;
    pPvt->pr = pr;
    pasynUser = pasynManager->createAsynUser(callback, 0);
    pasynUser->userPvt = pPvt;
    pPvt->pasynUser = pasynUser;
    /* This device support only supports 32-bit integer data types */
    if ((pwf->ftvl != menuFtypeLONG) && (pwf->ftvl != menuFtypeULONG)) {
        errlogPrintf("devAsynWf::initCommon, %s field type must be LONG or ULONG\n",
                     pr->name);
        goto bad;
    }
    /* Parse the link to get addr and port */
    status = pasynEpicsUtils->parseLink(pasynUser, plink,
                &pPvt->portName, &pPvt->addr, &pPvt->userParam);
    if (status != asynSuccess) {
        errlogPrintf("devAsynWf::initCommon, %s error in link %s\n",
                     pr->name, pasynUser->errorMessage);
        goto bad;
    }
    status = pasynManager->connectDevice(pasynUser, pPvt->portName, pPvt->addr);
    if (status != asynSuccess) {
        errlogPrintf("devAsynWf::initCommon, %s connectDevice failed %s\n",
                     pr->name, pasynUser->errorMessage);
        goto bad;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynDrvUserType,1);
    if(pasynInterface) {
        asynDrvUser *pasynDrvUser;
        void       *drvPvt;

        pasynDrvUser = (asynDrvUser *)pasynInterface->pinterface;
        drvPvt = pasynInterface->drvPvt;
        status = pasynDrvUser->create(drvPvt,pasynUser,
            pPvt->userParam,0,0);
        if(status!=asynSuccess) {
            errlogPrintf(
                "devAsynLong::initCommon, %s drvUserInit failed %s\n",
                pr->name, pasynUser->errorMessage);
            goto bad;
        }
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynInt32ArrayType,1);
    if(!pasynInterface) {
        errlogPrintf(
            "devAsynWf::initCommon, %s find %s interface failed %s\n",
            pr->name, asynInt32ArrayType,pasynUser->errorMessage);
        goto bad;
    }
    pPvt->pint32Array = pasynInterface->pinterface;
    pPvt->int32ArrayPvt = pasynInterface->drvPvt;
    scanIoInit(&pPvt->ioScanPvt);
    pPvt->interruptCallback = interruptCallback;
    /* Determine if device can block */
    pasynManager->canBlock(pasynUser, &pPvt->canBlock);
    return 0;
bad:
   pr->pact=1;
   return -1;
}


static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt)
{
    devAsynWfPvt *pPvt = (devAsynWfPvt *)pr->dpvt;
    asynStatus status;

    /* If initCommon failed then pPvt->pint32Array is NULL, return error */
    if (!pPvt->pint32Array) return -1;

    if (cmd == 0) {
        /* Add to scan list.  Register interrupts */
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s devAsynInt32Array::getIoIntInfo registering interrupt\n",
            pr->name);
        status = pPvt->pint32Array->registerInterruptUser(
           pPvt->int32ArrayPvt, pPvt->pasynUser,
           pPvt->interruptCallback, pPvt, &pPvt->registrarPvt);
        if(status!=asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                      "%s devAsynInt32Array registerInterruptUser %s\n",
                      pr->name, pPvt->pasynUser->errorMessage);
        }
    } else {
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s devAsynInt32Array::getIoIntInfo cancelling interrupt\n",
             pr->name);
        status = pPvt->pint32Array->cancelInterruptUser(pPvt->int32ArrayPvt,
             pPvt->pasynUser, pPvt->registrarPvt);
        if(status!=asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                      "%s devAsynInt32Array cancelInterruptUser %s\n",
                      pr->name,pPvt->pasynUser->errorMessage);
        }
    }
    *iopvt = pPvt->ioScanPvt;
    return 0;
}


static long initWfOutInt32Array(waveformRecord *pwf)
{ return  initCommon((dbCommon *)pwf, (DBLINK *)&pwf->inp, 
    callbackWfOut, interruptCallbackOutput); } 

static long initWfInt32Array(waveformRecord *pwf)
{ return initCommon((dbCommon *)pwf, (DBLINK *)&pwf->inp, 
    callbackWf, interruptCallbackInput); } 

static long processCommon(dbCommon *pr)
{
    devAsynWfPvt *pPvt = (devAsynWfPvt *)pr->dpvt;
    waveformRecord *pwf = (waveformRecord *)pr;
    int status;

    if (!pPvt->gotValue && !pr->pact) {   /* This is an initial call from record */
        if(pPvt->canBlock) pr->pact = 1;
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) pr->pact = 0;
        if (status != asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s processCommon, error queuing request %s\n",
                 pr->name, pPvt->pasynUser->errorMessage);
            recGblSetSevr(pr, READ_ALARM, INVALID_ALARM);
        }
    }
    if (pPvt->gotValue) pwf->nord = pPvt->nord;
    pPvt->gotValue = 0;
    return 0;
} 

static void callbackWfOut(asynUser *pasynUser)
{
    devAsynWfPvt *pPvt = (devAsynWfPvt *)pasynUser->userPvt;
    waveformRecord *pwf = (waveformRecord *)pPvt->pr;
    int status;

    asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
              "%s devAsynWf::callbackWfOut\n",pwf->name);
    status = pPvt->pint32Array->write(pPvt->int32ArrayPvt,
        pPvt->pasynUser, pwf->bptr, pwf->nelm);
    if (status == asynSuccess) {
        pwf->udf=0;
    } else {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "%s devAsynWf::callbackWfOut write error %s\n",
              pwf->name, pasynUser->errorMessage);
        recGblSetSevr(pwf, WRITE_ALARM, INVALID_ALARM);
    }
    if(pwf->pact) callbackRequestProcessCallback(&pPvt->callback,pwf->prio,pwf);
} 

static void callbackWf(asynUser *pasynUser)
{
    devAsynWfPvt *pPvt = (devAsynWfPvt *)pasynUser->userPvt;
    waveformRecord *pwf = (waveformRecord *)pPvt->pr;
    rset *prset = (rset *)pwf->rset;
    int status;
    size_t nread;

    status = pPvt->pint32Array->read(pPvt->int32ArrayPvt,
        pPvt->pasynUser, pwf->bptr, pwf->nelm, &nread);
    asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
              "%s devAsynWf::callbackWf\n",pwf->name);
    if (status == asynSuccess) {
        pwf->udf=0;
        pwf->nord = nread;
    } else {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "%s devAsynWf::callbackWf read error %s\n",
              pwf->name, pasynUser->errorMessage);
        recGblSetSevr(pwf, READ_ALARM, INVALID_ALARM);
    }
    if (pwf->pact) {
        dbScanLock((dbCommon *)pwf);
        prset->process(pwf);
        dbScanUnlock((dbCommon *)pwf);
    }
}

static void interruptCallbackInput(void *drvPvt, asynUser *pasynUser, 
                epicsInt32 *value, size_t len)
{
    devAsynWfPvt *pPvt = (devAsynWfPvt *)drvPvt;
    waveformRecord *pwf = (waveformRecord *)pPvt->pr;
    dbCommon *pr = pPvt->pr;
    int i;
    epicsInt32 *pint32 = (epicsInt32 *)pwf->bptr;

    asynPrintIO(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        (char *)value, len*sizeof(epicsInt32),
        "%s devAsynInt32Array::interruptCallbackInput\n",
        pwf->name);
    if (len > pwf->nelm) len = pwf->nelm;
    for (i=0; i<len; i++) pint32[i] = value[i];
    pPvt->gotValue = 1;
    pPvt->nord = len;
    dbScanLock(pr);
    pr->rset->process(pr);
    dbScanUnlock(pr);
}

static void interruptCallbackOutput(void *drvPvt, asynUser *pasynUser, 
                epicsInt32 *value, size_t len)
{
    devAsynWfPvt *pPvt = (devAsynWfPvt *)drvPvt;
    dbCommon *pr = pPvt->pr;

    asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
        "%s devAsynInt32Array::interruptCallbackOutput not supported yet!\n",
        pr->name);
    /* scanOnce(pr); */
}

