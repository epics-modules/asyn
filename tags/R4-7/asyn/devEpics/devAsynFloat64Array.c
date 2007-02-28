/* devAsynFloat64Array.c */
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
#include <dbDefs.h>
#include <link.h>
#include <dbScan.h>
#include <callback.h>
#include <errlog.h>
#include <epicsMutex.h>
#include <cantProceed.h>
#include <dbCommon.h>
#include <waveformRecord.h>
#include <recSup.h>
#include <devSup.h>

#include <epicsExport.h>
#include "asynDriver.h"
#include "asynDrvUser.h"
#include "asynFloat64Array.h"
#include "asynEpicsUtils.h"
#include <epicsExport.h>

typedef struct devAsynWfPvt{
    dbCommon        *pr;
    asynUser        *pasynUser;
    asynFloat64Array  *pfloat64Array;
    void            *float64ArrayPvt;
    int             canBlock;
    CALLBACK        callback;
    IOSCANPVT       ioScanPvt;
    char            *portName;
    char            *userParam;
    int             addr;
}devAsynWfPvt;

static long initCommon(dbCommon *pr, DBLINK *plink, userCallback callback);
static long processCommon(dbCommon *pr);
static long initWfFloat64Array(waveformRecord *pwf);
static long initWfOutFloat64Array(waveformRecord *pwf);
/* processCommon callbacks */
static void callbackWf(asynUser *pasynUser);
static void callbackWfOut(asynUser *pasynUser);

typedef struct analogDset { /* analog  dset */
    long        number;
    DEVSUPFUN     dev_report;
    DEVSUPFUN     init;
    DEVSUPFUN     init_record;
    DEVSUPFUN     get_ioint_info;
    DEVSUPFUN     processCommon;/*(0)=>(success ) */
    DEVSUPFUN     special_linconv;
} analogDset;

analogDset asynFloat64ArrayWfIn =
    {6, 0, 0, initWfFloat64Array,    0, processCommon, 0};
analogDset asynFloat64ArrayWfOut =
    {6, 0, 0, initWfOutFloat64Array, 0, processCommon, 0};

epicsExportAddress(dset, asynFloat64ArrayWfIn);
epicsExportAddress(dset, asynFloat64ArrayWfOut);

static long initCommon(dbCommon *pr, DBLINK *plink, userCallback callback)
{
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
    pasynInterface = pasynManager->findInterface(pasynUser,asynFloat64ArrayType,1);
    if(!pasynInterface) {
        errlogPrintf(
            "devAsynWf::initCommon, %s find %s interface failed %s\n",
            pr->name, asynFloat64ArrayType,pasynUser->errorMessage);
        goto bad;
    }
    pPvt->pfloat64Array = pasynInterface->pinterface;
    pPvt->float64ArrayPvt = pasynInterface->drvPvt;
    /* Determine if device can block */
    pasynManager->canBlock(pasynUser, &pPvt->canBlock);
    return 0;
bad:
   pr->pact=1;
   return -1;
}

static long initWfOutFloat64Array(waveformRecord *pwf)
{ return  initCommon((dbCommon *)pwf, (DBLINK *)&pwf->inp, callbackWfOut); } 

static long initWfFloat64Array(waveformRecord *pwf)
{ return initCommon((dbCommon *)pwf, (DBLINK *)&pwf->inp, callbackWf); } 

static long processCommon(dbCommon *pr)
{
    devAsynWfPvt *pPvt = (devAsynWfPvt *)pr->dpvt;
    int status;

    if (pr->pact == 0) {   /* This is an initial call from record */
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
    return 0;
} 

static void callbackWfOut(asynUser *pasynUser)
{
    devAsynWfPvt *pPvt = (devAsynWfPvt *)pasynUser->userPvt;
    waveformRecord *pwf = (waveformRecord *)pPvt->pr;
    int status;

    asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
              "%s devAsynWf::callbackWfOut\n",pwf->name);
    status = pPvt->pfloat64Array->write(pPvt->float64ArrayPvt,
        pPvt->pasynUser, pwf->bptr, pwf->nelm);
    if (status == asynSuccess) {
        pwf->udf=0;
    } else {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "%s devAsynWf::callbackWfOut read error %s\n",
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

    status = pPvt->pfloat64Array->read(pPvt->float64ArrayPvt,
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

