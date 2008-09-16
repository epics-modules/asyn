#define ASYN_XXX_ARRAY_FUNCS(DRIVER_NAME, INTERFACE, INTERFACE_TYPE, \
                             INTERRUPT, EPICS_TYPE, DSET_IN, DSET_OUT, \
                             SIGNED_TYPE, UNSIGNED_TYPE) \
/* devAsynXXXArray.h */ \
/*********************************************************************** \
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne \
* National Laboratory, and the Regents of the University of \
* California, as Operator of Los Alamos National Laboratory, and \
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY). \
* asynDriver is distributed subject to a Software License Agreement \
* found in file LICENSE that is included with this distribution. \
***********************************************************************/ \
/* \
    Author:  Geoff Savage \
    19NOV2004 \
    \
    Modified by Mark Rivers, converted to giant macro \
    March 26, 2008 \
*/ \
 \
 \
typedef struct devAsynWfPvt{ \
    dbCommon        *pr; \
    asynUser        *pasynUser; \
    INTERFACE       *pArray; \
    void            *arrayPvt; \
    void            *registrarPvt; \
    int             canBlock; \
    CALLBACK        callback; \
    IOSCANPVT       ioScanPvt; \
    asynStatus      status; \
    int             gotValue; /*For interruptCallbackInput */ \
    int             nord; \
    INTERRUPT       interruptCallback; \
    char            *portName; \
    char            *userParam; \
    int             addr; \
} devAsynWfPvt; \
 \
static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt); \
static long initCommon(dbCommon *pr, DBLINK *plink,  \
    userCallback callback, INTERRUPT interruptCallback); \
static long processCommon(dbCommon *pr); \
static long initWfArrayIn(waveformRecord *pwf); \
static long initWfArrayOut(waveformRecord *pwf); \
/* processCommon callbacks */ \
static void callbackWf(asynUser *pasynUser); \
static void callbackWfOut(asynUser *pasynUser); \
static void interruptCallbackInput(void *drvPvt, asynUser *pasynUser,  \
                EPICS_TYPE *value, size_t len); \
static void interruptCallbackOutput(void *drvPvt, asynUser *pasynUser,  \
                EPICS_TYPE *value, size_t len); \
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
analogDset DSET_IN = \
    {6, 0, 0, initWfArrayIn,    getIoIntInfo, processCommon, 0}; \
analogDset DSET_OUT = \
    {6, 0, 0, initWfArrayOut, getIoIntInfo, processCommon, 0}; \
 \
epicsExportAddress(dset, DSET_IN); \
epicsExportAddress(dset, DSET_OUT); \
\
static char *driverName = DRIVER_NAME; \
 \
static long initCommon(dbCommon *pr, DBLINK *plink,  \
    userCallback callback, INTERRUPT interruptCallback) \
{ \
    waveformRecord *pwf = (waveformRecord *)pr; \
    devAsynWfPvt *pPvt; \
    asynStatus status; \
    asynUser *pasynUser; \
    asynInterface *pasynInterface; \
 \
    pPvt = callocMustSucceed(1, sizeof(*pPvt), "devAsynWf::initCommon"); \
    pr->dpvt = pPvt; \
    pPvt->pr = pr; \
    pasynUser = pasynManager->createAsynUser(callback, 0); \
    pasynUser->userPvt = pPvt; \
    pPvt->pasynUser = pasynUser; \
    /* This device support only supports signed and unsigned versions of the EPICS data type  */ \
    if ((pwf->ftvl != SIGNED_TYPE) && (pwf->ftvl != UNSIGNED_TYPE)) { \
        errlogPrintf("%s::initCommon, %s field type must be SIGNED_TYPE or UNSIGNED_TYPE\n", \
                     driverName, pr->name); \
        goto bad; \
    } \
    /* Parse the link to get addr and port */ \
    status = pasynEpicsUtils->parseLink(pasynUser, plink, \
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
    if(pasynInterface) { \
        asynDrvUser *pasynDrvUser; \
        void       *drvPvt; \
 \
        pasynDrvUser = (asynDrvUser *)pasynInterface->pinterface; \
        drvPvt = pasynInterface->drvPvt; \
        status = pasynDrvUser->create(drvPvt,pasynUser, \
            pPvt->userParam,0,0); \
        if(status!=asynSuccess) { \
            errlogPrintf( \
                "devAsynLong::initCommon, %s drvUserInit failed %s\n", \
                pr->name, pasynUser->errorMessage); \
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
    pPvt->pArray = pasynInterface->pinterface; \
    pPvt->arrayPvt = pasynInterface->drvPvt; \
    scanIoInit(&pPvt->ioScanPvt); \
    pPvt->interruptCallback = interruptCallback; \
    /* Determine if device can block */ \
    pasynManager->canBlock(pasynUser, &pPvt->canBlock); \
    return 0; \
bad: \
   pr->pact=1; \
   return -1; \
} \
 \
 \
static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt) \
{ \
    devAsynWfPvt *pPvt = (devAsynWfPvt *)pr->dpvt; \
    asynStatus status; \
 \
    /* If initCommon failed then pPvt->pArray is NULL, return error */ \
    if (!pPvt->pArray) return -1; \
 \
    if (cmd == 0) { \
        /* Add to scan list.  Register interrupts */ \
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW, \
            "%s %s::getIoIntInfo registering interrupt\n", \
            pr->name, driverName); \
        status = pPvt->pArray->registerInterruptUser( \
           pPvt->arrayPvt, pPvt->pasynUser, \
           pPvt->interruptCallback, pPvt, &pPvt->registrarPvt); \
        if(status!=asynSuccess) { \
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR, \
                      "%s %s registerInterruptUser %s\n", \
                      pr->name, driverName, pPvt->pasynUser->errorMessage); \
        } \
    } else { \
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW, \
            "%s %s::getIoIntInfo cancelling interrupt\n", \
             pr->name, driverName); \
        status = pPvt->pArray->cancelInterruptUser(pPvt->arrayPvt, \
             pPvt->pasynUser, pPvt->registrarPvt); \
        if(status!=asynSuccess) { \
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR, \
                      "%s %s cancelInterruptUser %s\n", \
                      pr->name,pPvt->pasynUser->errorMessage); \
        } \
    } \
    *iopvt = pPvt->ioScanPvt; \
    return 0; \
} \
 \
 \
static long initWfArrayOut(waveformRecord *pwf) \
{ return  initCommon((dbCommon *)pwf, (DBLINK *)&pwf->inp,  \
    callbackWfOut, interruptCallbackOutput); }  \
 \
static long initWfArrayIn(waveformRecord *pwf) \
{ return initCommon((dbCommon *)pwf, (DBLINK *)&pwf->inp,  \
    callbackWf, interruptCallbackInput); }  \
 \
static long processCommon(dbCommon *pr) \
{ \
    devAsynWfPvt *pPvt = (devAsynWfPvt *)pr->dpvt; \
    waveformRecord *pwf = (waveformRecord *)pr; \
    int status; \
 \
    if (!pPvt->gotValue && !pr->pact) {   /* This is an initial call from record */ \
        if(pPvt->canBlock) pr->pact = 1; \
        status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0); \
        if((status==asynSuccess) && pPvt->canBlock) return 0; \
        if(pPvt->canBlock) pr->pact = 0; \
        if (status != asynSuccess) { \
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR, \
                "%s processCommon, error queuing request %s\n", \
                 pr->name, pPvt->pasynUser->errorMessage); \
            recGblSetSevr(pr, READ_ALARM, INVALID_ALARM); \
        } \
    } \
    if (pPvt->gotValue) { \
	pwf->nord = pPvt->nord; \
        pwf->udf = 0; \
    } \
    pPvt->gotValue = 0; \
    return 0; \
}  \
 \
static void callbackWfOut(asynUser *pasynUser) \
{ \
    devAsynWfPvt *pPvt = (devAsynWfPvt *)pasynUser->userPvt; \
    waveformRecord *pwf = (waveformRecord *)pPvt->pr; \
    int status; \
 \
    asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, \
              "%s %s::callbackWfOut\n", pwf->name, driverName); \
    status = pPvt->pArray->write(pPvt->arrayPvt, \
        pPvt->pasynUser, pwf->bptr, pwf->nelm); \
    if (status == asynSuccess) { \
        pwf->udf=0; \
    } else { \
        asynPrint(pasynUser, ASYN_TRACE_ERROR, \
              "%s %s::callbackWfOut write error %s\n", \
              pwf->name, driverName, pasynUser->errorMessage); \
        recGblSetSevr(pwf, WRITE_ALARM, INVALID_ALARM); \
    } \
    if(pwf->pact) callbackRequestProcessCallback(&pPvt->callback,pwf->prio,pwf); \
}  \
 \
static void callbackWf(asynUser *pasynUser) \
{ \
    devAsynWfPvt *pPvt = (devAsynWfPvt *)pasynUser->userPvt; \
    waveformRecord *pwf = (waveformRecord *)pPvt->pr; \
    int status; \
    size_t nread; \
 \
    status = pPvt->pArray->read(pPvt->arrayPvt, \
        pPvt->pasynUser, pwf->bptr, pwf->nelm, &nread); \
    asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, \
              "%s %s::callbackWf\n", pwf->name, driverName); \
    if (status == asynSuccess) { \
        pwf->udf=0; \
        pwf->nord = nread; \
    } else { \
        asynPrint(pasynUser, ASYN_TRACE_ERROR, \
              "%s %s::callbackWf read error %s\n", \
              pwf->name, driverName, pasynUser->errorMessage); \
        recGblSetSevr(pwf, READ_ALARM, INVALID_ALARM); \
    } \
    if(pwf->pact) callbackRequestProcessCallback(&pPvt->callback,pwf->prio,pwf); \
} \
 \
static void interruptCallbackInput(void *drvPvt, asynUser *pasynUser,  \
                EPICS_TYPE *value, size_t len) \
{ \
    devAsynWfPvt *pPvt = (devAsynWfPvt *)drvPvt; \
    waveformRecord *pwf = (waveformRecord *)pPvt->pr; \
    int i; \
    EPICS_TYPE *pData = (EPICS_TYPE *)pwf->bptr; \
 \
    asynPrintIO(pPvt->pasynUser, ASYN_TRACEIO_DEVICE, \
        (char *)value, len*sizeof(EPICS_TYPE), \
        "%s %s::interruptCallbackInput\n", \
        pwf->name, driverName); \
    if (len > pwf->nelm) len = pwf->nelm; \
    for (i=0; i<(int)len; i++) pData[i] = value[i]; \
    pPvt->gotValue = 1; \
    pPvt->nord = len; \
    scanIoRequest(pPvt->ioScanPvt); \
} \
 \
static void interruptCallbackOutput(void *drvPvt, asynUser *pasynUser,  \
                EPICS_TYPE *value, size_t len) \
{ \
    devAsynWfPvt *pPvt = (devAsynWfPvt *)drvPvt; \
    dbCommon *pr = pPvt->pr; \
 \
    asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR, \
        "%s %s::interruptCallbackOutput not supported yet!\n", \
        pr->name, driverName); \
    /* scanOnce(pr); */ \
} \

