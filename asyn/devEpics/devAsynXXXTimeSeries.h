#define ASYN_XXX_TIME_SERIES_FUNCS(DRIVER_NAME, INTERFACE, INTERFACE_TYPE, \
                                   INTERRUPT, EPICS_TYPE, DSET, \
                                   SIGNED_TYPE, UNSIGNED_TYPE) \
/* devAsynXXXTimeSeries.h */ \
/*********************************************************************** \
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne \
* National Laboratory, and the Regents of the University of \
* California, as Operator of Los Alamos National Laboratory, and \
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY). \
* asynDriver is distributed subject to a Software License Agreement \
* found in file LICENSE that is included with this distribution. \
***********************************************************************/ \
/* \
    Author: Mark Rivers \
    Feb. 8, 2012 \
*/ \
 \
 \
typedef struct devAsynWfPvt{ \
    dbCommon        *pr; \
    asynUser        *pasynUser; \
    INTERFACE       *pInterface; \
    void            *ifacePvt; \
    void            *registrarPvt; \
    CALLBACK        callback; \
    int             busy; \
    epicsUInt32     nord; \
    char            *portName; \
    char            *userParam; \
    epicsMutexId    lock; \
    int             addr; \
    asynStatus      status; \
} devAsynWfPvt; \
 \
static long initRecord(dbCommon *pr); \
static long process(dbCommon *pr); \
static void interruptCallback(void *drvPvt, asynUser *pasynUser,  \
                EPICS_TYPE value); \
 \
typedef struct analogDset { /* analog  dset */ \
    long        number; \
    DEVSUPFUN     dev_report; \
    DEVSUPFUN     init; \
    DEVSUPFUN     init_record; \
    DEVSUPFUN     get_ioint_info; \
    DEVSUPFUN     processCommon;/*(0)=>(success ) */ \
    DEVSUPFUN     special_linconv; \
} analogDset; \
 \
analogDset DSET = \
    {6, 0, 0, initRecord,    0, process, 0}; \
 \
epicsExportAddress(dset, DSET); \
\
static char *driverName = DRIVER_NAME; \
 \
static long initRecord(dbCommon *pr) \
{ \
    waveformRecord *pwf = (waveformRecord *)pr; \
    devAsynWfPvt *pPvt; \
    asynStatus status; \
    asynUser *pasynUser; \
    asynInterface *pasynInterface; \
 \
    pPvt = callocMustSucceed(1, sizeof(*pPvt), "devAsynXXXTimerSeries::initRecord"); \
    pr->dpvt = pPvt; \
    pPvt->pr = pr; \
    pPvt->lock = epicsMutexCreate(); \
    pasynUser = pasynManager->createAsynUser(0, 0); \
    pasynUser->userPvt = pPvt; \
    pPvt->pasynUser = pasynUser; \
    /* This device support only supports signed and unsigned versions of the EPICS data type  */ \
    if ((pwf->ftvl != SIGNED_TYPE) && (pwf->ftvl != UNSIGNED_TYPE)) { \
        errlogPrintf("%s::initCommon, %s field type must be SIGNED_TYPE or UNSIGNED_TYPE\n", \
                     driverName, pr->name); \
        goto bad; \
    } \
    /* Parse the link to get addr and port */ \
    status = pasynEpicsUtils->parseLink(pasynUser, (DBLINK *)&pwf->inp, \
                &pPvt->portName, &pPvt->addr, &pPvt->userParam); \
    if (status != asynSuccess) { \
        errlogPrintf("%s::initCommon, %s error in link %s\n", \
                     driverName, pr->name, pasynUser->errorMessage); \
        goto bad; \
    } \
    status = pasynManager->connectDevice(pasynUser, pPvt->portName, pPvt->addr); \
    if (status != asynSuccess) { \
        errlogPrintf("%s::initCommon, %s connectDevice failed %s\n", \
                     driverName, pr->name, pasynUser->errorMessage); \
        goto bad; \
    } \
    pasynInterface = pasynManager->findInterface(pasynUser,asynDrvUserType,1); \
    if(pasynInterface && pPvt->userParam) { \
        asynDrvUser *pasynDrvUser; \
        void       *drvPvt; \
 \
        pasynDrvUser = (asynDrvUser *)pasynInterface->pinterface; \
        drvPvt = pasynInterface->drvPvt; \
        status = pasynDrvUser->create(drvPvt,pasynUser, \
            pPvt->userParam,0,0); \
        if(status!=asynSuccess) { \
            errlogPrintf( \
                "%s::initCommon, %s drvUserCreate failed %s\n", \
                driverName, pr->name, pasynUser->errorMessage); \
            goto bad; \
        } \
    } \
    pasynInterface = pasynManager->findInterface(pasynUser,INTERFACE_TYPE,1); \
    if(!pasynInterface) { \
        errlogPrintf( \
            "%s::initCommon, %s find %s interface failed %s\n", \
            driverName, pr->name, INTERFACE_TYPE,pasynUser->errorMessage); \
        goto bad; \
    } \
    pPvt->pInterface = pasynInterface->pinterface; \
    pPvt->ifacePvt = pasynInterface->drvPvt; \
    return 0; \
bad: \
   pr->pact=1; \
   return -1; \
} \
 \
 \
static long process(dbCommon *pr) \
{ \
    devAsynWfPvt *pPvt = (devAsynWfPvt *)pr->dpvt; \
    waveformRecord *pwf = (waveformRecord *)pr; \
    int busy; \
    asynStatus status; \
    epicsAlarmCondition alarmStat; \
    epicsAlarmSeverity alarmSevr; \
 \
    epicsMutexLock(pPvt->lock); \
    busy = pPvt->busy; \
    switch(pwf->rarm) { \
      case 0: \
        break; \
      case 1: \
        pPvt->nord = 0; \
        busy = 1; \
        memset(pwf->bptr, 0, pwf->nelm*sizeof(EPICS_TYPE)); \
        break; \
      case 2: \
        busy = 0; \
        break; \
      case 3: \
        busy = 1; \
        break; \
    } \
    if (pwf->nord != pPvt->nord) { \
      pwf->nord = pPvt->nord; \
      db_post_events(pwf, &pwf->nord, DBE_VALUE | DBE_LOG); \
    } \
    if (pwf->busy != busy) { \
      pwf->busy = busy; \
      db_post_events(pwf, &pwf->busy, DBE_VALUE | DBE_LOG); \
      /* BUSY has changed state so either register or cancel callbacks */ \
      if (busy) { \
        status = pPvt->pInterface->registerInterruptUser( \
           pPvt->ifacePvt, pPvt->pasynUser, \
           interruptCallback, pPvt, &pPvt->registrarPvt); \
        if(status!=asynSuccess) { \
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR, \
                "%s %s registerInterruptUser %s\n", \
                pr->name, driverName, pPvt->pasynUser->errorMessage); \
        } \
      } \
      else {\
        status = pPvt->pInterface->cancelInterruptUser( \
           pPvt->ifacePvt, pPvt->pasynUser, pPvt->registrarPvt); \
        if(status!=asynSuccess) { \
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR, \
                "%s %s cancelInterruptUser %s\n", \
                pr->name, driverName, pPvt->pasynUser->errorMessage); \
        } \
      } \
    } \
    pPvt->busy = pwf->busy; \
    pwf->rarm = 0; \
    pwf->udf = 0; \
    if (pPvt->status != asynSuccess) { \
        pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->status, READ_ALARM, &alarmStat, \
                                                INVALID_ALARM, &alarmSevr); \
        recGblSetSevr(pr, alarmStat, alarmSevr); \
    } \
    epicsMutexUnlock(pPvt->lock); \
    pPvt->status = asynSuccess; \
    return 0; \
}  \
 \
static void interruptCallback(void *drvPvt, asynUser *pasynUser, EPICS_TYPE value) \
{ \
    devAsynWfPvt *pPvt = (devAsynWfPvt *)drvPvt; \
    waveformRecord *pwf = (waveformRecord *)pPvt->pr; \
    EPICS_TYPE *pData = (EPICS_TYPE *)pwf->bptr; \
 \
    epicsMutexLock(pPvt->lock); \
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE, \
        "%s %s::interruptCallback, value=%f, nord=%d\n", \
        pwf->name, driverName, (double)value, pPvt->nord); \
    /* If we are not acquiring then nothing to do */  \
    if (pPvt->busy) { \
      if (pPvt->nord < pwf->nelm) { \
        pData[pPvt->nord] = value; \
        pPvt->nord++; \
      } \
      else { \
        pPvt->busy = 0; \
        /* When acquisition completes process record */ \
        callbackRequestProcessCallback(&pPvt->callback,pwf->prio,pwf); \
      } \
    } \
    if (pPvt->status == asynSuccess) pPvt->status = pasynUser->auxStatus; \
    epicsMutexUnlock(pPvt->lock); \
} \

