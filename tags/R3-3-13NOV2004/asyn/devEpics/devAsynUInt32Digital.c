/* devAsynUInt32Digital.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*  Author:  Mark Rivers

   This file provides device support for the following records for the
   asynUInt32Digital and asynUInt32DigitalCallback interfaces.
      longin
      longout
      binary input (bi)
      binary output (bo)
*/

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <ctype.h>

#include <alarm.h>
#include <recGbl.h>
#include <dbAccess.h>
#include <dbDefs.h>
#include <link.h>
#include <cantProceed.h>
#include <dbCommon.h>
#include <dbScan.h>
#include <recSup.h>
#include <devSup.h>
#include <biRecord.h>
#include <boRecord.h>
#include <mbbiRecord.h>
#include <mbboRecord.h>
#include <mbbiDirectRecord.h>
#include <mbboDirectRecord.h>
#include <longinRecord.h>
#include <longoutRecord.h>
#include <epicsPrint.h>
#include <epicsExport.h>

#include "asynDriver.h"
#include "asynDrvUser.h"
#include "asynUInt32Digital.h"
#include "asynUInt32DigitalCallback.h"
#include "asynEpicsUtils.h"

typedef struct {
   asynUser *pasynUser;
   asynUInt32Digital *pasynUInt32D;
   void              *asynUInt32DPvt;
   asynUInt32DigitalCallback *pasynUInt32DCb;
   void                      *asynUInt32DCbPvt;
   epicsUInt32 value;
   epicsUInt32 mask;
   int         callbacksSupported;
   int         callbacksEnabled;
   IOSCANPVT   ioScanPvt;
   char        *portName;
   char        *userParam;
} devDigitalPvt;

typedef struct  {
    long       number;
    DEVSUPFUN  report;
    DEVSUPFUN  init;
    DEVSUPFUN  init_record;
    DEVSUPFUN  get_ioint_info;
    DEVSUPFUN  io;
} dsetDigital;

typedef enum {recTypeBi, recTypeBo, recTypeLi, recTypeLo, 
    recTypeMbbi, recTypeMbbo,recTypeMbbiDirect, recTypeMbboDirect
}recType;

static long initCommon(dbCommon *pr, DBLINK *plink, userCallback callback,
                       recType rt);
static long processCallback(dbCommon *pr);
static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt);
static void dataCallback(void *v, epicsUInt32 value);
static long initBi(biRecord *pbi);
static void callbackBi(asynUser *pasynUser);
static long initLi(longinRecord *pli);
static void callbackLi(asynUser *pasynUser);
static long initBo(boRecord *pbo);
static void callbackBo(asynUser *pasynUser);
static long initLo(longoutRecord *plo);
static void callbackLo(asynUser *pasynUser);
static long initMbbi(mbbiRecord *pmbbi);
static void callbackMbbi(asynUser *pasynUser);
static long initMbbo(mbboRecord *pmbbo);
static void callbackMbbo(asynUser *pasynUser);
static long initMbbiDirect(mbbiDirectRecord *pmbbiDirect);
static void callbackMbbiDirect(asynUser *pasynUser);
static long initMbboDirect(mbboDirectRecord *pmbboDirect);
static void callbackMbboDirect(asynUser *pasynUser);


dsetDigital asynBiUInt32Digital = {5, 0, 0, initBi, getIoIntInfo, processCallback};
dsetDigital asynLiUInt32Digital = {5, 0, 0, initLi, getIoIntInfo, processCallback};
dsetDigital asynBoUInt32Digital = {5, 0, 0, initBo, 0,            processCallback};
dsetDigital asynLoUInt32Digital = {5, 0, 0, initLo, 0,            processCallback};
dsetDigital asynMbbiUInt32Digital = {5, 0, 0, initMbbi, 0,        processCallback};
dsetDigital asynMbboUInt32Digital = {5, 0, 0, initMbbo, 0,        processCallback};
dsetDigital asynMbbiDirectUInt32Digital = {5, 0, 0, initMbbiDirect, 0,        processCallback};
dsetDigital asynMbboDirectUInt32Digital = {5, 0, 0, initMbboDirect, 0,        processCallback};

epicsExportAddress(dset, asynBiUInt32Digital);
epicsExportAddress(dset, asynLiUInt32Digital);
epicsExportAddress(dset, asynBoUInt32Digital);
epicsExportAddress(dset, asynLoUInt32Digital);
epicsExportAddress(dset, asynMbbiUInt32Digital);
epicsExportAddress(dset, asynMbboUInt32Digital);
epicsExportAddress(dset, asynMbbiDirectUInt32Digital);
epicsExportAddress(dset, asynMbboDirectUInt32Digital);


static long initCommon(dbCommon *pr, DBLINK *plink, userCallback callback,
                       recType rt)
{
    devDigitalPvt *pPvt;
    asynStatus    status;
    asynUser      *pasynUser;
    asynInterface *pasynInterface;
    int           addr;

    pPvt = callocMustSucceed(1, sizeof(*pPvt), "devAsynUInt32Digital::initCommon");
    pr->dpvt = pPvt;
    /* Create asynUser */
    pasynUser = pasynManager->createAsynUser(callback, 0);
    pasynUser->userPvt = pr;
    pPvt->pasynUser = pasynUser;

    status = pasynEpicsUtils->parseLink(pasynUser, plink,
                &pPvt->portName, &addr, &pPvt->userParam);
    if (status != asynSuccess) {
        printf("devAsynUInt32Digital::initCommon, %s error parsing link %s\n",
                     pr->name, pasynUser->errorMessage);
        goto bad;
    }
    if ((rt == recTypeBi) || (rt == recTypeBo)) {
        if(addr<0 || addr>31) {
            printf("%s devAsynUInt32Digital::initCommon: Illegal addr field"
                         " (0-31)=%d\n",
                         pr->name, addr);
            goto bad;
        }
        pPvt->mask =  1 << addr;
    } else {
        pPvt->mask = 0xffffffff;
    }
    /* Connect to device */
    status = pasynManager->connectDevice(pasynUser, pPvt->portName, addr);
    if (status != asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "%s devAsynUInt32Digital::initCommon connectDevice failed %s\n",
            pr->name, pasynUser->errorMessage);
        goto bad;
    }
    /*call drvUserCreate*/
    pasynInterface = pasynManager->findInterface(pasynUser,asynDrvUserType,1);
    if(pasynInterface && pPvt->userParam) {
        asynDrvUser *pasynDrvUser;
        void       *drvPvtCommon;

        pasynDrvUser = (asynDrvUser *)pasynInterface->pinterface;
        drvPvtCommon = pasynInterface->drvPvt;
        status = pasynDrvUser->create(drvPvtCommon,pasynUser,
            pPvt->userParam,0,0);
        if(status!=asynSuccess) {
            printf("devAsynUInt32Digital::initCommon, %s drvUserCreate failed %s\n",
                     pr->name, pasynUser->errorMessage);
            goto bad;
        }
    }

    /* Get the asynUInt32Digital interface */
    pasynInterface = pasynManager->findInterface(pasynUser, 
                                                 asynUInt32DigitalType, 1);
    if (!pasynInterface) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "%s devAsynUInt32Digital::initCommon find asynUInt32Digital"
            " interface failed\n", pr->name);
        goto bad;
    }
    pPvt->pasynUInt32D = (asynUInt32Digital *)pasynInterface->pinterface;
    pPvt->asynUInt32DPvt = pasynInterface->drvPvt;

    /* Get the asynUInt32DigitalCallback interface */
    pasynInterface = pasynManager->findInterface(pasynUser, 
                                                 asynUInt32DigitalCallbackType, 1);
    if (pasynInterface == 0) {
        pPvt->callbacksSupported = 0;
        pPvt->pasynUInt32DCb = 0;
        pPvt->asynUInt32DCbPvt = 0;
    } else {
        pPvt->callbacksSupported = 1;
        pPvt->pasynUInt32DCb = (asynUInt32DigitalCallback *)
                                                    pasynInterface->pinterface;
        pPvt->asynUInt32DCbPvt = pasynInterface->drvPvt;
    }
    if (pPvt->callbacksSupported && 
        ((rt == recTypeBi) || (rt == recTypeLi) || (rt == recTypeMbbi)
        || (rt == recTypeMbbiDirect))) {
        scanIoInit(&pPvt->ioScanPvt);
    }
    return(0);

bad:
    pr->pact=1;
    return(-1);
}


static long initBi(biRecord *pbi)
{
    initCommon((dbCommon *)pbi, &pbi->inp, callbackBi, recTypeBi);
    return(0);
}

static long initBo(boRecord *pbo)
{
    initCommon((dbCommon *)pbo, &pbo->out, callbackBo, recTypeBo);
    return(2); /* Do not convert */
}

static long initLi(longinRecord *pli)
{
    initCommon((dbCommon *)pli, &pli->inp, callbackLi, recTypeLi);
    return(0);
}

static long initLo(longoutRecord *plo)
{
    initCommon((dbCommon *)plo, &plo->out, callbackLo, recTypeLo);
    return(2); /* Do not convert */
}

static long initMbbi(mbbiRecord *pmbbi)
{
    initCommon((dbCommon *)pmbbi, &pmbbi->inp, callbackMbbi, recTypeMbbi);
    if(pmbbi->nobt == 0) pmbbi->mask = 0xffffffff;
    pmbbi->mask <<= pmbbi->shft;
    return(0);
}

static long initMbbo(mbboRecord *pmbbo)
{
    initCommon((dbCommon *)pmbbo, &pmbbo->out, callbackMbbo, recTypeMbbo);
    if(pmbbo->nobt == 0) pmbbo->mask = 0xffffffff;
    pmbbo->mask <<= pmbbo->shft;
    /* don't convert */
    return(2);
}

static long initMbbiDirect(mbbiDirectRecord *pmbbiDirect)
{
    initCommon((dbCommon *)pmbbiDirect, &pmbbiDirect->inp,
        callbackMbbiDirect, recTypeMbbiDirect);
    if(pmbbiDirect->nobt == 0) pmbbiDirect->mask = 0xffffffff;
    pmbbiDirect->mask <<= pmbbiDirect->shft;
    return(0);
}

static long initMbboDirect(mbboDirectRecord *pmbboDirect)
{
    initCommon((dbCommon *)pmbboDirect, &pmbboDirect->out,
        callbackMbboDirect, recTypeMbboDirect);
    if(pmbboDirect->nobt == 0) pmbboDirect->mask = 0xffffffff;
    pmbboDirect->mask <<= pmbboDirect->shft;
    /* don't convert */
    return(2);
}

static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt)
{
    devDigitalPvt *pPvt = (devDigitalPvt *)pr->dpvt;

    if (!pPvt->callbacksSupported) return(0);
    if (cmd == 0) {
        /* Add to scan list.  Enable callbacks */
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s devAsynUInt32Digital::getIoIntInfo registering callback\n",
            pr->name);
        pPvt->callbacksEnabled = 1;
        pPvt->pasynUInt32DCb->registerCallback(pPvt->asynUInt32DCbPvt, 
                                               pPvt->pasynUser,
                                               dataCallback,
                                               pPvt->mask, pr);
    } else {
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s devAsynUInt32Digital::getIoIntInfo cancelling callback\n",
             pr->name);
        pPvt->callbacksEnabled = 0;
        pPvt->pasynUInt32DCb->cancelCallback(pPvt->asynUInt32DCbPvt, 
                                             pPvt->pasynUser,
                                             dataCallback,
                                             pPvt->mask, pr);
    }
    *iopvt = pPvt->ioScanPvt;
    return 0;
}

static long processCallback(dbCommon *pr)
{
    devDigitalPvt *pPvt = (devDigitalPvt *)pr->dpvt;
    asynStatus status;

    status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
    if (status != asynSuccess) {
        asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR, 
            "%s devAsynUInt32Digital::queueRequest error %s\n",
            pr->name, pPvt->pasynUser->errorMessage);
        recGblSetSevr(pr, COMM_ALARM, INVALID_ALARM);
        return(-1);
    }
    return(0);
}

static void callbackBi(asynUser *pasynUser)
{
    biRecord *pbi = (biRecord *)pasynUser->userPvt;
    devDigitalPvt *pPvt = (devDigitalPvt *)pbi->dpvt;
    asynStatus status;

    /* If callbacks not enabled, need to read value */
    if (!pPvt->callbacksEnabled) {
        status = pPvt->pasynUInt32D->read(pPvt->asynUInt32DPvt, pasynUser,
                                          &pPvt->value, pPvt->mask);
        asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
                  "%s devAsynUInt32Digital::callbackBi read=%x\n", 
                   pbi->name, pPvt->value);
        if (status == asynSuccess) {
            pbi->udf=0;
        } else {
            recGblSetSevr(pbi, READ_ALARM, INVALID_ALARM);
        }
    }
    pbi->rval = (pPvt->value & pPvt->mask) ? 1 : 0;
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynUInt32Digital::callbackBi value=%d\n",pbi->name, pbi->rval);
}

static void callbackLi(asynUser *pasynUser)
{
    longinRecord *pli = (longinRecord *)pasynUser->userPvt;
    devDigitalPvt *pPvt = (devDigitalPvt *)pli->dpvt;
    asynStatus status;

    /* If callbacks not enabled, need to read value */
    if (!pPvt->callbacksEnabled) {
        status = pPvt->pasynUInt32D->read(pPvt->asynUInt32DPvt, pasynUser, 
                                          &pPvt->value, pPvt->mask);
        if (status == asynSuccess) {
            pli->udf=0;
        } else {
            recGblSetSevr(pli, READ_ALARM, INVALID_ALARM);
        }
    }
    pli->val = pPvt->value;
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynUInt32Digital::callbackLi value=%x\n", pli->name, pli->val);
}

static void callbackMbbi(asynUser *pasynUser)
{
    mbbiRecord *pmbbi = (mbbiRecord *)pasynUser->userPvt;
    devDigitalPvt *pPvt = (devDigitalPvt *)pmbbi->dpvt;
    asynStatus status;

    /* If callbacks not enabled, need to read value */
    if (!pPvt->callbacksEnabled) {
        status = pPvt->pasynUInt32D->read(pPvt->asynUInt32DPvt, pasynUser, 
                                          &pPvt->value, pPvt->mask);
        if (status == asynSuccess) {
            pmbbi->udf=0;
        } else {
            recGblSetSevr(pmbbi, READ_ALARM, INVALID_ALARM);
        }
    }
    pmbbi->rval = pPvt->value & pmbbi->mask;
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynUInt32Digital::callbackMbbi value=%x\n", pmbbi->name, pmbbi->rval);
}

static void callbackMbbiDirect(asynUser *pasynUser)
{
    mbbiDirectRecord *pmbbiDirect = (mbbiDirectRecord *)pasynUser->userPvt;
    devDigitalPvt *pPvt = (devDigitalPvt *)pmbbiDirect->dpvt;
    asynStatus status;

    /* If callbacks not enabled, need to read value */
    if (!pPvt->callbacksEnabled) {
        status = pPvt->pasynUInt32D->read(pPvt->asynUInt32DPvt, pasynUser, 
                                          &pPvt->value, pPvt->mask);
        if (status == asynSuccess) {
            pmbbiDirect->udf=0;
        } else {
            recGblSetSevr(pmbbiDirect, READ_ALARM, INVALID_ALARM);
        }
    }
    pmbbiDirect->rval = pPvt->value & pmbbiDirect->mask;
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynUInt32Digital::callbackMbbiDirect value=%x\n",
        pmbbiDirect->name, pmbbiDirect->rval);
}

static void callbackBo(asynUser *pasynUser)
{
    boRecord *pbo = (boRecord *)pasynUser->userPvt;
    devDigitalPvt *pPvt = (devDigitalPvt *)pbo->dpvt;
    asynStatus status;
    epicsUInt32 data;

    if (pbo->val == 0) data = 0;
    else               data = 0xffffffff & pPvt->mask;
    status = pPvt->pasynUInt32D->write(pPvt->asynUInt32DPvt, 
                                       pPvt->pasynUser, data, pPvt->mask);
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynUInt32Digital::callbackBo write=%x\n", pbo->name, data);
    if (status != asynSuccess)
        recGblSetSevr(pbo, WRITE_ALARM, INVALID_ALARM);
}


static void callbackLo(asynUser *pasynUser)
{
    longoutRecord *plo = (longoutRecord *)pasynUser->userPvt;
    devDigitalPvt *pPvt = (devDigitalPvt *)plo->dpvt;
    asynStatus status;

    status = pPvt->pasynUInt32D->write(pPvt->asynUInt32DPvt,
                                       pPvt->pasynUser, plo->val, pPvt->mask);
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynUInt32Digital::callbackLo write=%x\n", plo->name, plo->val);
    if (status != asynSuccess)
        recGblSetSevr(plo, WRITE_ALARM, INVALID_ALARM);
}

static void callbackMbbo(asynUser *pasynUser)
{
    mbboRecord *pmbbo = (mbboRecord *)pasynUser->userPvt;
    devDigitalPvt *pPvt = (devDigitalPvt *)pmbbo->dpvt;
    asynStatus status;
    epicsUInt32 value;

    value = pmbbo->rval & pmbbo->mask;
    status = pPvt->pasynUInt32D->write(pPvt->asynUInt32DPvt,
                                       pPvt->pasynUser, value, pmbbo->mask);
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynUInt32Digital::callbackMbbo write=%x\n", pmbbo->name, value);
    if (status != asynSuccess)
        recGblSetSevr(pmbbo, WRITE_ALARM, INVALID_ALARM);
}

static void callbackMbboDirect(asynUser *pasynUser)
{
    mbboDirectRecord *pmbboDirect = (mbboDirectRecord *)pasynUser->userPvt;
    devDigitalPvt *pPvt = (devDigitalPvt *)pmbboDirect->dpvt;
    asynStatus status;
    epicsUInt32 value;

    value = pmbboDirect->rval & pmbboDirect->mask;
    status = pPvt->pasynUInt32D->write(pPvt->asynUInt32DPvt,
                                pPvt->pasynUser, value, pmbboDirect->mask);
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynUInt32Digital::callbackMbboDirect write=%x\n",
        pmbboDirect->name, value);
    if (status != asynSuccess)
        recGblSetSevr(pmbboDirect, WRITE_ALARM, INVALID_ALARM);
}

static void dataCallback(void *v, epicsUInt32 value)
{
    dbCommon *pr = (dbCommon *)v;
    devDigitalPvt *pPvt = (devDigitalPvt *)pr->dpvt;

    pPvt->value = value;
    pr->udf = 0;
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s devAsynUInt32Digital::dataCallback, new value=%x\n",pr->name,value);
    scanIoRequest(pPvt->ioScanPvt);
}
