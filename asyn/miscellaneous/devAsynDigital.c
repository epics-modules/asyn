/* devIpUnidig.c 

    Author:  Mark Rivers
    28-June-2004 Converted from MPF to asyn
                 Handle interrupts differently, so some channels can be 
                 interrupt driven, others polled.

   This file provides device support for the following records for the
   Greensprings Unidig digital I/O IP module.
      longin
      longout
      binary input (bi)
      binary output (bo)

*/

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

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
#include <longinRecord.h>
#include <longoutRecord.h>
#include <epicsPrint.h>
#include <epicsExport.h>

#include "asynDriver.h"
#include "asynInt32.h"
#include "asynIpUnidig.h"
#include "asynUtils.h"

typedef struct {
   asynUser *pasynUser;
   asynInt32 *pasynInt32;
   void *asynInt32Pvt;
   asynIpUnidig *pasynIpUnidig;
   void *asynIpUnidigPvt;
   epicsUInt32 value;
   epicsUInt32 mask;
   int callbacksEnabled;
   IOSCANPVT ioScanPvt;
} devIpUnidigPvt;

typedef struct  {
    long       number;
    DEVSUPFUN  report;
    DEVSUPFUN  init;
    DEVSUPFUN  init_record;
    DEVSUPFUN  get_ioint_info;
    DEVSUPFUN  io;
} dsetIpUnidig;

typedef enum {recTypeBi, recTypeBo, recTypeLi, recTypeLo} recType;

static long initCommon(dbCommon *pr, DBLINK *plink, userCallback callback,
                       recType rt);
static long queueRequest(dbCommon *pr);
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


dsetIpUnidig devBiIpUnidig = {5, 0, 0, initBi, getIoIntInfo, queueRequest};
dsetIpUnidig devLiIpUnidig = {5, 0, 0, initLi, getIoIntInfo, queueRequest};
dsetIpUnidig devBoIpUnidig = {5, 0, 0, initBo, 0, queueRequest};
dsetIpUnidig devLoIpUnidig = {5, 0, 0, initLo, 0, queueRequest};

epicsExportAddress(dset, devBiIpUnidig);
epicsExportAddress(dset, devLiIpUnidig);
epicsExportAddress(dset, devBoIpUnidig);
epicsExportAddress(dset, devLoIpUnidig);


static long initCommon(dbCommon *pr, DBLINK *plink, userCallback callback,
                       recType rt)
{
    devIpUnidigPvt *pPvt;
    char *port, *userParam;
    int card, signal;
    asynStatus status;
    asynUser *pasynUser;
    asynInterface *pasynInterface;

    pPvt = callocMustSucceed(1, sizeof(*pPvt), "devIpUnidig::initCommon");
    pr->dpvt = pPvt;
    /* Create asynUser */
    pasynUser = pasynManager->createAsynUser(callback, 0);
    pasynUser->userPvt = pr;
    pPvt->pasynUser = pasynUser;

    status = pasynUtils->parseVmeIo(pasynUser, plink, &card, &signal, &port,
                                    &userParam);
    if (status != asynSuccess) {
        errlogPrintf("devIpUnidig::initCommon, %s error parsing link %s\n",
                     pr->name, pasynUser->errorMessage);
        goto bad;
    }
    if ((rt == recTypeBi) || (rt == recTypeBo)) {
        if(signal<0 || signal>23) {
            errlogPrintf("%s devIpUnidig::initCommon: Illegal signal field"
                         " (0-23)=%d\n",
                         pr->name, signal);
            goto bad;
        }
        pPvt->mask =  1 << signal;
    } else {
        pPvt->mask = 0xffffff;
    }
    if ((rt == recTypeBi) || (rt == recTypeLi)) {
        scanIoInit(&pPvt->ioScanPvt);
    }
    /* Connect to device */
    status = pasynManager->connectDevice(pasynUser, port, 0);
    if (status != asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "devIpUnidig::initCommon, connectDevice failed %s\n",
                  pasynUser->errorMessage);
        goto bad;
    }

    /* Get the asynInt32 interface */
    pasynInterface = pasynManager->findInterface(pasynUser, asynInt32Type, 1);
    if (!pasynInterface) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "devIpUnidig::initCommon, find asynInt32 interface failed\n");
        goto bad;
    }
    pPvt->pasynInt32 = (asynInt32 *)pasynInterface->pinterface;
    pPvt->asynInt32Pvt = pasynInterface->drvPvt;

    /* Get the asynIpUnidig interface */
    pasynInterface = pasynManager->findInterface(pasynUser, asynIpUnidigType, 1);
    if (!pasynInterface) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "devIpUnidig::initCommon, find asynIpUnidig interface failed\n");
        goto bad;
    }
    pPvt->pasynIpUnidig = (asynIpUnidig *)pasynInterface->pinterface;
    pPvt->asynIpUnidigPvt = pasynInterface->drvPvt;
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

static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt)
{
    devIpUnidigPvt *pPvt = (devIpUnidigPvt *)pr->dpvt;

    if (cmd == 0) {
        /* Add to scan list.  Enable callbacks */
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
                  "devIpUnidig::getIoIntInfo %s registering callback\n",
                  pr->name);
        pPvt->callbacksEnabled = 1;
        pPvt->pasynIpUnidig->registerCallback(pPvt->asynIpUnidigPvt, 
                                              pPvt->pasynUser,
                                              dataCallback,
                                              pPvt->mask, pr);
    } else {
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
                  "devIpUnidig::getIoIntInfo %s cancelling callback\n",
                  pr->name);
        pPvt->callbacksEnabled = 0;
        pPvt->pasynIpUnidig->cancelCallback(pPvt->asynIpUnidigPvt, 
                                            pPvt->pasynUser,
                                            dataCallback,
                                            pPvt->mask, pr);
    }
    *iopvt = pPvt->ioScanPvt;
    return 0;
}

static long queueRequest(dbCommon *pr)
{
    devIpUnidigPvt *pPvt = (devIpUnidigPvt *)pr->dpvt;
    asynStatus status;

    status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
    if (status != asynSuccess) {
        asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR, 
                  "devIpUnidig::queueRequest %s error %s\n",
                  pr->name, pPvt->pasynUser->errorMessage);
        recGblSetSevr(pr, COMM_ALARM, INVALID_ALARM);
        return(-1);
    }
    return(0);
}

static void callbackBi(asynUser *pasynUser)
{
    biRecord *pbi = (biRecord *)pasynUser->userPvt;
    devIpUnidigPvt *pPvt = (devIpUnidigPvt *)pbi->dpvt;
    asynStatus status;

    /* If callbacks not enabled, need to read value */
    if (!pPvt->callbacksEnabled) {
        status = pPvt->pasynInt32->read(pPvt->asynInt32Pvt, pasynUser,
                                        &pPvt->value);
        asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
                  "devIpUnidig::callbackBi %s read=%x\n", 
                   pbi->name, pPvt->value);
        if (status == asynSuccess) {
            pbi->udf=0;
        } else {
            recGblSetSevr(pbi, READ_ALARM, INVALID_ALARM);
        }
    }
    pbi->rval = (pPvt->value & pPvt->mask) ? 1 : 0;
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "devIpUnidig::callbackBi %s value=%d\n", 
              pbi->name, pbi->rval);
}

static void callbackLi(asynUser *pasynUser)
{
    longinRecord *pli = (longinRecord *)pasynUser->userPvt;
    devIpUnidigPvt *pPvt = (devIpUnidigPvt *)pli->dpvt;
    asynStatus status;

    /* If callbacks not enabled, need to read value */
    if (!pPvt->callbacksEnabled) {
        status = pPvt->pasynInt32->read(pPvt->asynInt32Pvt, pasynUser, 
                                        &pPvt->value);
        if (status == asynSuccess) {
            pli->udf=0;
        } else {
            recGblSetSevr(pli, READ_ALARM, INVALID_ALARM);
        }
    }
    pli->val = pPvt->value;
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "devIpUnidig::callbackLi %s, value=%x\n", 
              pli->name, pli->val);
}


static void callbackBo(asynUser *pasynUser)
{
    boRecord *pbo = (boRecord *)pasynUser->userPvt;
    devIpUnidigPvt *pPvt = (devIpUnidigPvt *)pbo->dpvt;
    asynStatus status;

    if (pbo->val == 0) {
        status = pPvt->pasynIpUnidig->clearBits(pPvt->asynIpUnidigPvt, 
                                                pPvt->pasynUser, pPvt->mask);
        asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
                  "devIpUnidig::callbackBo %s, clearBits=%x\n", 
                  pbo->name, pPvt->mask);
    } else {
        status = pPvt->pasynIpUnidig->setBits(pPvt->asynIpUnidigPvt, 
                                              pPvt->pasynUser, pPvt->mask);
        asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
                  "devIpUnidig::callbackBo %s, setBits=%x\n", 
                  pbo->name, pPvt->mask);
    }
    if (status != asynSuccess)
        recGblSetSevr(pbo, WRITE_ALARM, INVALID_ALARM);
}


static void callbackLo(asynUser *pasynUser)
{
    longoutRecord *plo = (longoutRecord *)pasynUser->userPvt;
    devIpUnidigPvt *pPvt = (devIpUnidigPvt *)plo->dpvt;
    asynStatus status;

    status = pPvt->pasynInt32->write(pPvt->asynInt32Pvt,
                                     pPvt->pasynUser, plo->val);
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "devIpUnidig::callbackLo %s, value=%x\n",
              plo->name, plo->val);
    if (status != asynSuccess)
        recGblSetSevr(plo, WRITE_ALARM, INVALID_ALARM);
}


static void dataCallback(void *v, epicsUInt32 value)
{
    dbCommon *pr = (dbCommon *)v;
    devIpUnidigPvt *pPvt = (devIpUnidigPvt *)pr->dpvt;

    pPvt->value = value;
    pr->udf = 0;
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "devIpUnidig::dataCallback, new value=%x\n", value);
    scanIoRequest(pPvt->ioScanPvt);
}
