#define INIT_OK 0
#define INIT_DO_NOT_CONVERT 2
#define INIT_ERROR -1

#define DEFAULT_RING_BUFFER_SIZE 0

#define ASYN_XXX_ARRAY_FUNCS(DRIVER_NAME, INTERFACE, INTERFACE_TYPE,                               \
                             INTERRUPT, EPICS_TYPE, DSET_IN, DSET_OUT,                             \
                             SIGNED_TYPE, UNSIGNED_TYPE)                                           \
/* devAsynXXXArray.h */                                                                            \
/***********************************************************************                           \
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne                             \
* National Laboratory, and the Regents of the University of                                        \
* California, as Operator of Los Alamos National Laboratory, and                                   \
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).                                     \
* asynDriver is distributed subject to a Software License Agreement                                \
* found in file LICENSE that is included with this distribution.                                   \
***********************************************************************/                           \
/*                                                                                                 \
    Oroginal author:  Geoff Savage                                                                 \
    19NOV2004                                                                                      \
                                                                                                   \
    Current author: Mark Rivers                                                                    \
*/                                                                                                 \
                                                                                                   \
                                                                                                   \
typedef struct ringBufferElement {                                                                 \
    EPICS_TYPE      *pValue;                                                                       \
    size_t          len;                                                                           \
    epicsTimeStamp  time;                                                                          \
    asynStatus      status;                                                                        \
} ringBufferElement;                                                                               \
                                                                                                   \
typedef struct devAsynWfPvt{                                                                       \
    dbCommon            *pr;                                                                       \
    asynUser            *pasynUser;                                                                \
    INTERFACE           *pArray;                                                                   \
    void                *arrayPvt;                                                                 \
    void                *registrarPvt;                                                             \
    int                 canBlock;                                                                  \
    CALLBACK            callback;                                                                  \
    IOSCANPVT           ioScanPvt;                                                                 \
    asynStatus          status;                                                                    \
    int                 isOutput;                                                                  \
    epicsAlarmCondition alarmStat;                                                                 \
    epicsAlarmSeverity  alarmSevr;                                                                 \
    epicsMutexId        ringBufferLock;                                                            \
    ringBufferElement   *ringBuffer;                                                               \
    int                 ringHead;                                                                  \
    int                 ringTail;                                                                  \
    int                 ringSize;                                                                  \
    int                 ringBufferOverflows;                                                       \
    ringBufferElement   result;                                                                    \
    int                 gotValue; /* For interruptCallbackInput */                                 \
    INTERRUPT           interruptCallback;                                                         \
    char                *portName;                                                                 \
    char                *userParam;                                                                \
    int                 addr;                                                                      \
    asynStatus          previousQueueRequestStatus;                                                \
} devAsynWfPvt;                                                                                    \
                                                                                                   \
static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt);                                 \
static long initCommon(dbCommon *pr, DBLINK *plink,                                                \
    userCallback callback, INTERRUPT interruptCallback, int isOutput);                             \
static long processCommon(dbCommon *pr);                                                           \
static long initWfArrayIn(waveformRecord *pwf);                                                    \
static long initWfArrayOut(waveformRecord *pwf);                                                   \
/* processCommon callbacks */                                                                      \
static void callbackWfIn(asynUser *pasynUser);                                                     \
static void callbackWfOut(asynUser *pasynUser);                                                    \
static int getRingBufferValue(devAsynWfPvt *pPvt);                                                 \
static long createRingBuffer(dbCommon *pr);                                                        \
static void interruptCallback(void *drvPvt, asynUser *pasynUser,                                   \
                EPICS_TYPE *value, size_t len);                                                    \
                                                                                                   \
typedef struct analogDset { /* analog  dset */                                                     \
    long        number;                                                                            \
    DEVSUPFUN     dev_report;                                                                      \
    DEVSUPFUN     init;                                                                            \
    DEVSUPFUN     init_record;                                                                     \
    DEVSUPFUN     get_ioint_info;                                                                  \
    DEVSUPFUN     processCommon;/*(0)=>(success ) */                                               \
    DEVSUPFUN     special_linconv;                                                                 \
} analogDset;                                                                                      \
                                                                                                   \
analogDset DSET_IN =                                                                               \
    {6, 0, 0, initWfArrayIn,  getIoIntInfo, processCommon, 0};                                     \
analogDset DSET_OUT =                                                                              \
    {6, 0, 0, initWfArrayOut, getIoIntInfo, processCommon, 0};                                     \
                                                                                                   \
epicsExportAddress(dset, DSET_IN);                                                                 \
epicsExportAddress(dset, DSET_OUT);                                                                \
                                                                                                   \
static char *driverName = DRIVER_NAME;                                                             \
                                                                                                   \
static long initCommon(dbCommon *pr, DBLINK *plink,                                                \
    userCallback callback, INTERRUPT interruptCallback, int isOutput)                              \
{                                                                                                  \
    waveformRecord *pwf = (waveformRecord *)pr;                                                    \
    devAsynWfPvt *pPvt;                                                                            \
    int status;                                                                                    \
    asynUser *pasynUser;                                                                           \
    asynInterface *pasynInterface;                                                                 \
                                                                                                   \
    pPvt = callocMustSucceed(1, sizeof(*pPvt), "devAsynXXXArray::initCommon");                     \
    pr->dpvt = pPvt;                                                                               \
    pPvt->pr = pr;                                                                                 \
    pPvt->isOutput = isOutput;                                                                     \
    pPvt->interruptCallback = interruptCallback;                                                   \
    pasynUser = pasynManager->createAsynUser(callback, 0);                                         \
    pasynUser->userPvt = pPvt;                                                                     \
    pPvt->pasynUser = pasynUser;                                                                   \
    pPvt->ringBufferLock = epicsMutexCreate();                                                     \
    /* This device support only supports signed and unsigned versions of the EPICS data type  */   \
    if ((pwf->ftvl != SIGNED_TYPE) && (pwf->ftvl != UNSIGNED_TYPE)) {                              \
        errlogPrintf("%s::initCommon, %s field type must be SIGNED_TYPE or UNSIGNED_TYPE\n",       \
                     driverName, pr->name);                                                        \
        goto bad;                                                                                  \
    }                                                                                              \
    /* Parse the link to get addr and port */                                                      \
    status = pasynEpicsUtils->parseLink(pasynUser, plink,                                          \
                &pPvt->portName, &pPvt->addr, &pPvt->userParam);                                   \
    if (status != asynSuccess) {                                                                   \
        errlogPrintf("%s::initCommon, %s error in link %s\n",                                      \
                     driverName, pr->name, pasynUser->errorMessage);                               \
        goto bad;                                                                                  \
    }                                                                                              \
    status = pasynManager->connectDevice(pasynUser, pPvt->portName, pPvt->addr);                   \
    if (status != asynSuccess) {                                                                   \
        errlogPrintf("%s::initCommon, %s connectDevice failed %s\n",                               \
                     driverName, pr->name, pasynUser->errorMessage);                               \
        goto bad;                                                                                  \
    }                                                                                              \
    pasynInterface = pasynManager->findInterface(pasynUser,asynDrvUserType,1);                     \
    if(pasynInterface && pPvt->userParam) {                                                        \
        asynDrvUser *pasynDrvUser;                                                                 \
        void       *drvPvt;                                                                        \
                                                                                                   \
        pasynDrvUser = (asynDrvUser *)pasynInterface->pinterface;                                  \
        drvPvt = pasynInterface->drvPvt;                                                           \
        status = pasynDrvUser->create(drvPvt,pasynUser,                                            \
            pPvt->userParam,0,0);                                                                  \
        if(status!=asynSuccess) {                                                                  \
            errlogPrintf(                                                                          \
                "%s::initCommon, %s drvUserCreate failed %s\n",                                    \
                driverName, pr->name, pasynUser->errorMessage);                                    \
            goto bad;                                                                              \
        }                                                                                          \
    }                                                                                              \
    pasynInterface = pasynManager->findInterface(pasynUser,INTERFACE_TYPE,1);                      \
    if(!pasynInterface) {                                                                          \
        errlogPrintf(                                                                              \
            "%s::initCommon, %s find %s interface failed %s\n",                                    \
            driverName, pr->name, INTERFACE_TYPE,pasynUser->errorMessage);                         \
        goto bad;                                                                                  \
    }                                                                                              \
    pPvt->pArray = pasynInterface->pinterface;                                                     \
    pPvt->arrayPvt = pasynInterface->drvPvt;                                                       \
    /* If this is an output record and the info field "asyn:READBACK" is 1                         \
     * then register for callbacks on output records */                                            \
    if (pPvt->isOutput) {                                                                          \
        int enableCallbacks=0;                                                                     \
        const char *callbackString;                                                                \
        DBENTRY *pdbentry = dbAllocEntry(pdbbase);                                                 \
        status = dbFindRecord(pdbentry, pr->name);                                                 \
        if (status) {                                                                              \
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,                                           \
                "%s %s::initCommon error finding record\n",                                        \
                pr->name, driverName);                                                             \
            goto bad;                                                                              \
        }                                                                                          \
        callbackString = dbGetInfo(pdbentry, "asyn:READBACK");                                     \
        if (callbackString) enableCallbacks = atoi(callbackString);                                \
        if (enableCallbacks) {                                                                     \
            status = createRingBuffer(pr);                                                         \
            if (status != asynSuccess) goto bad;                                                   \
            status = pPvt->pArray->registerInterruptUser(                                          \
               pPvt->arrayPvt, pPvt->pasynUser,                                                    \
               pPvt->interruptCallback, pPvt, &pPvt->registrarPvt);                                \
            if(status != asynSuccess) {                                                            \
                printf("%s %s::initCommon error calling registerInterruptUser %s\n",               \
                       pr->name, driverName, pPvt->pasynUser->errorMessage);                       \
            }                                                                                      \
        }                                                                                          \
    }                                                                                              \
    scanIoInit(&pPvt->ioScanPvt);                                                                  \
    /* Determine if device can block */                                                            \
    pasynManager->canBlock(pasynUser, &pPvt->canBlock);                                            \
    return 0;                                                                                      \
bad:                                                                                               \
    recGblSetSevr(pr,LINK_ALARM,INVALID_ALARM);                                                    \
    pr->pact=1;                                                                                    \
    return INIT_ERROR;                                                                             \
}                                                                                                  \
                                                                                                   \
static long createRingBuffer(dbCommon *pr)                                                         \
{                                                                                                  \
    devAsynWfPvt *pPvt = (devAsynWfPvt *)pr->dpvt;                                                 \
    asynStatus status;                                                                             \
    waveformRecord *pwf = (waveformRecord *)pr;                                                    \
    const char *sizeString;                                                                        \
                                                                                                   \
    if (!pPvt->ringBuffer) {                                                                       \
        DBENTRY *pdbentry = dbAllocEntry(pdbbase);                                                 \
        pPvt->ringSize = DEFAULT_RING_BUFFER_SIZE;                                                 \
        status = dbFindRecord(pdbentry, pr->name);                                                 \
        if (status)                                                                                \
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,                                           \
                "%s %s::getIoIntInfo error finding record\n",                                      \
                pr->name, driverName);                                                             \
        sizeString = dbGetInfo(pdbentry, "asyn:FIFO");                                             \
        if (sizeString) pPvt->ringSize = atoi(sizeString);                                         \
        if (pPvt->ringSize > 0) {                                                                  \
            int i;                                                                                 \
            pPvt->ringBuffer = callocMustSucceed(                                                  \
                                   pPvt->ringSize, sizeof(*pPvt->ringBuffer),                      \
                                   "devAsynXXXArray::getIoIntInfo creating ring buffer");          \
            /* Allocate array for each ring buffer element */                                      \
            for (i=0; i<pPvt->ringSize; i++) {                                                     \
                pPvt->ringBuffer[i].pValue =                                                       \
                    (EPICS_TYPE *)callocMustSucceed(                                               \
                        pwf->nelm, sizeof(EPICS_TYPE),                                             \
                        "devAsynXXXArray::getIoIntInfo creating ring element array");              \
            }                                                                                      \
        }                                                                                          \
    }                                                                                              \
    return asynSuccess;                                                                            \
}                                                                                                  \
                                                                                                   \
static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt)                                  \
{                                                                                                  \
    devAsynWfPvt *pPvt = (devAsynWfPvt *)pr->dpvt;                                                 \
    int status;                                                                                    \
                                                                                                   \
    /* If initCommon failed then pPvt->pArray is NULL, return error */                             \
    if (!pPvt->pArray) return -1;                                                                  \
                                                                                                   \
    if (cmd == 0) {                                                                                \
        /* Add to scan list.  Register interrupts */                                               \
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,                                                \
            "%s %s::getIoIntInfo registering interrupt\n",                                         \
            pr->name, driverName);                                                                 \
        createRingBuffer(pr);                                                                      \
        status = pPvt->pArray->registerInterruptUser(                                              \
           pPvt->arrayPvt, pPvt->pasynUser,                                                        \
           pPvt->interruptCallback, pPvt, &pPvt->registrarPvt);                                    \
        if(status!=asynSuccess) {                                                                  \
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,                                           \
                      "%s %s::getIoIntInfo registerInterruptUser %s\n",                            \
                      pr->name, driverName, pPvt->pasynUser->errorMessage);                        \
        }                                                                                          \
    } else {                                                                                       \
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,                                                \
            "%s %s::getIoIntInfo cancelling interrupt\n",                                          \
             pr->name, driverName);                                                                \
        status = pPvt->pArray->cancelInterruptUser(pPvt->arrayPvt,                                 \
             pPvt->pasynUser, pPvt->registrarPvt);                                                 \
        if(status!=asynSuccess) {                                                                  \
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,                                           \
                      "%s %s::getIoIntInfo cancelInterruptUser %s\n",                              \
                      pr->name, driverName,pPvt->pasynUser->errorMessage);                         \
        }                                                                                          \
    }                                                                                              \
    *iopvt = pPvt->ioScanPvt;                                                                      \
    return INIT_OK;                                                                                \
}                                                                                                  \
                                                                                                   \
static void reportQueueRequestStatus(devAsynWfPvt *pPvt, asynStatus status)                        \
{                                                                                                  \
    if (pPvt->previousQueueRequestStatus != status) {                                              \
        pPvt->previousQueueRequestStatus = status;                                                 \
        if (status == asynSuccess) {                                                               \
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,                                           \
                "%s %s queueRequest status returned to normal\n",                                  \
                pPvt->pr->name, driverName);                                                       \
        } else {                                                                                   \
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,                                           \
                "%s %s queueRequest %s\n",                                                         \
                pPvt->pr->name, driverName, pPvt->pasynUser->errorMessage);                        \
        }                                                                                          \
    }                                                                                              \
}                                                                                                  \
                                                                                                   \
static long initWfArrayOut(waveformRecord *pwf)                                                    \
{ return  initCommon((dbCommon *)pwf, (DBLINK *)&pwf->inp,                                         \
    callbackWfOut, interruptCallback, 1); }                                                        \
                                                                                                   \
static long initWfArrayIn(waveformRecord *pwf)                                                     \
{ return initCommon((dbCommon *)pwf, (DBLINK *)&pwf->inp,                                          \
    callbackWfIn, interruptCallback, 0); }                                                         \
                                                                                                   \
static long processCommon(dbCommon *pr)                                                            \
{                                                                                                  \
    devAsynWfPvt *pPvt = (devAsynWfPvt *)pr->dpvt;                                                 \
    waveformRecord *pwf = (waveformRecord *)pr;                                                    \
    int newInputData;                                                                              \
                                                                                                   \
    if (pPvt->ringSize == 0) {                                                                     \
        newInputData = pPvt->gotValue;                                                             \
    } else {                                                                                       \
        newInputData = getRingBufferValue(pPvt);                                                   \
    }                                                                                              \
    if (!newInputData && !pr->pact) {   /* This is an initial call from record */                  \
        if(pPvt->canBlock) pr->pact = 1;                                                           \
        pPvt->status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);                          \
        if((pPvt->status==asynSuccess) && pPvt->canBlock) return 0;                                \
        if(pPvt->canBlock) pr->pact = 0;                                                           \
        reportQueueRequestStatus(pPvt, pPvt->status);                                                    \
    }                                                                                              \
    if (newInputData) {                                                                            \
        if (pPvt->ringSize == 0){                                                                  \
            /* Data has already been copied to the record in interruptCallback */                  \
            pPvt->gotValue--;                                                                      \
            if (pPvt->gotValue) {                                                                  \
                asynPrint(pPvt->pasynUser, ASYN_TRACE_WARNING,                                     \
                    "%s %s::processCommon, "                                                       \
                    "warning: multiple interrupt callbacks between processing\n",                  \
                     pr->name, driverName);                                                        \
            }                                                                                      \
        } else {                                                                                   \
            /* Copy data from ring buffer */                                                       \
            EPICS_TYPE *pData = (EPICS_TYPE *)pwf->bptr;                                           \
            ringBufferElement *rp = &pPvt->result;                                                 \
            int i;                                                                                 \
            /* Need to copy the array with the lock because that is shared even though             \
               pPvt->result is a copy */                                                           \
            if (rp->status == asynSuccess) {                                                       \
                epicsMutexLock(pPvt->ringBufferLock);                                              \
                for (i=0; i<(int)rp->len; i++) pData[i] = rp->pValue[i];                           \
                epicsMutexUnlock(pPvt->ringBufferLock);                                            \
                pwf->nord = rp->len;                                                               \
                asynPrintIO(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,                                  \
                    (char *)pwf->bptr, pwf->nord*sizeof(EPICS_TYPE),                               \
                    "%s %s::processCommon nord=%d, pwf->bptr data:",                               \
                    pwf->name, driverName, pwf->nord);                                             \
            }                                                                                      \
            pwf->time = rp->time;                                                                  \
            pPvt->status = rp->status;                                                             \
        }                                                                                          \
    }                                                                                              \
    if (pPvt->status == asynSuccess) {                                                             \
        pwf->udf = 0;                                                                              \
        return 0;                                                                                  \
    } else {                                                                                       \
        pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->status, READ_ALARM, &pPvt->alarmStat,        \
                                                INVALID_ALARM, &pPvt->alarmSevr);                  \
        recGblSetSevr(pr, pPvt->alarmStat, pPvt->alarmSevr);                                       \
        pPvt->status = asynSuccess;                                                                \
        return -1;                                                                                 \
    }                                                                                              \
}                                                                                                  \
                                                                                                   \
static void callbackWfOut(asynUser *pasynUser)                                                     \
{                                                                                                  \
    devAsynWfPvt *pPvt = (devAsynWfPvt *)pasynUser->userPvt;                                       \
    waveformRecord *pwf = (waveformRecord *)pPvt->pr;                                              \
    int status;                                                                                    \
                                                                                                   \
    asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,                                                      \
              "%s %s::callbackWfOut\n", pwf->name, driverName);                                    \
    status = pPvt->pArray->write(pPvt->arrayPvt,                                                   \
        pPvt->pasynUser, pwf->bptr, pwf->nord);                                                    \
    if (status == asynSuccess) {                                                                   \
        pwf->udf=0;                                                                                \
    } else {                                                                                       \
        asynPrint(pasynUser, ASYN_TRACE_ERROR,                                                     \
              "%s %s::callbackWfOut write error %s\n",                                             \
              pwf->name, driverName, pasynUser->errorMessage);                                     \
        pasynEpicsUtils->asynStatusToEpicsAlarm(status, WRITE_ALARM, &pPvt->alarmStat,             \
                                                INVALID_ALARM, &pPvt->alarmSevr);                  \
        recGblSetSevr(pwf, pPvt->alarmStat, pPvt->alarmSevr);                                      \
    }                                                                                              \
    if(pwf->pact) callbackRequestProcessCallback(&pPvt->callback,pwf->prio,pwf);                   \
}                                                                                                  \
                                                                                                   \
static void callbackWfIn(asynUser *pasynUser)                                                      \
{                                                                                                  \
    devAsynWfPvt *pPvt = (devAsynWfPvt *)pasynUser->userPvt;                                       \
    waveformRecord *pwf = (waveformRecord *)pPvt->pr;                                              \
    int status;                                                                                    \
    size_t nread;                                                                                  \
                                                                                                   \
    status = pPvt->pArray->read(pPvt->arrayPvt,                                                    \
        pPvt->pasynUser, pwf->bptr, pwf->nelm, &nread);                                            \
    asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,                                                      \
              "%s %s::callbackWfIn\n", pwf->name, driverName);                                     \
    pwf->time = pasynUser->timestamp;                                                              \
    if (status == asynSuccess) {                                                                   \
        pwf->udf=0;                                                                                \
        pwf->nord = (epicsUInt32)nread;                                                            \
    } else {                                                                                       \
        asynPrint(pasynUser, ASYN_TRACE_ERROR,                                                     \
              "%s %s::callbackWfIn read error %s\n",                                               \
              pwf->name, driverName, pasynUser->errorMessage);                                     \
        pasynEpicsUtils->asynStatusToEpicsAlarm(status, READ_ALARM, &pPvt->alarmStat,              \
                                                INVALID_ALARM, &pPvt->alarmSevr);                  \
        recGblSetSevr(pwf, pPvt->alarmStat, pPvt->alarmSevr);                                      \
    }                                                                                              \
    if(pwf->pact) callbackRequestProcessCallback(&pPvt->callback,pwf->prio,pwf);                   \
}                                                                                                  \
                                                                                                   \
static int getRingBufferValue(devAsynWfPvt *pPvt)                                                  \
{                                                                                                  \
    int ret = 0;                                                                                   \
    epicsMutexLock(pPvt->ringBufferLock);                                                          \
    if (pPvt->ringTail != pPvt->ringHead) {                                                        \
        if (pPvt->ringBufferOverflows > 0) {                                                       \
            asynPrint(pPvt->pasynUser, ASYN_TRACE_WARNING,                                         \
                "%s %s::getRingBufferValue error, %d ring buffer overflows\n",                     \
                pPvt->pr->name, driverName, pPvt->ringBufferOverflows);                            \
            pPvt->ringBufferOverflows = 0;                                                         \
        }                                                                                          \
        pPvt->result = pPvt->ringBuffer[pPvt->ringTail];                                           \
        pPvt->ringTail = (pPvt->ringTail==pPvt->ringSize-1) ? 0 : pPvt->ringTail+1;                \
        ret = 1;                                                                                   \
    }                                                                                              \
    epicsMutexUnlock(pPvt->ringBufferLock);                                                        \
    return ret;                                                                                    \
}                                                                                                  \
                                                                                                   \
static void interruptCallback(void *drvPvt, asynUser *pasynUser,                                   \
                EPICS_TYPE *value, size_t len)                                                     \
{                                                                                                  \
    devAsynWfPvt *pPvt = (devAsynWfPvt *)drvPvt;                                                   \
    waveformRecord *pwf = (waveformRecord *)pPvt->pr;                                              \
    int i;                                                                                         \
    EPICS_TYPE *pData = (EPICS_TYPE *)pwf->bptr;                                                   \
                                                                                                   \
    asynPrintIO(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,                                              \
        (char *)value, len*sizeof(EPICS_TYPE),                                                     \
        "%s %s::interruptCallbackInput ringSize=%d, len=%d, callback data:",                       \
        pwf->name, driverName, pPvt->ringSize, (int)len);                                          \
    if (pPvt->ringSize == 0) {                                                                     \
        /* Not using a ring buffer */                                                              \
        dbScanLock((dbCommon *)pwf);                                                               \
        if (len > pwf->nelm) len = pwf->nelm;                                                      \
        if (pasynUser->auxStatus == asynSuccess) {                                                 \
            for (i=0; i<(int)len; i++) pData[i] = value[i];                                        \
            pwf->nord = (epicsUInt32)len;                                                          \
        }                                                                                          \
        pwf->time = pasynUser->timestamp;                                                          \
        pPvt->gotValue++;                                                                          \
        if (pPvt->status == asynSuccess) pPvt->status = pasynUser->auxStatus;                      \
        dbScanUnlock((dbCommon *)pwf);                                                             \
        if (pPvt->isOutput)                                                                        \
            scanOnce((dbCommon *)pwf);                                                             \
        else                                                                                       \
            scanIoRequest(pPvt->ioScanPvt);                                                        \
    } else {                                                                                       \
        /* Using a ring buffer */                                                                  \
        ringBufferElement *rp;                                                                     \
                                                                                                   \
        /* If interruptAccept is false we just return.  This prevents more ring pushes than pops.  \
         * There will then be nothing in the ring buffer, so the first                             \
         * read will do a read from the driver, which should be OK. */                             \
        if (!interruptAccept) return;                                                              \
                                                                                                   \
        epicsMutexLock(pPvt->ringBufferLock);                                                      \
        rp = &pPvt->ringBuffer[pPvt->ringHead];                                                    \
        if (len > pwf->nelm) len = pwf->nelm;                                                      \
        rp->len = len;                                                                             \
        for (i=0; i<(int)len; i++) rp->pValue[i] = value[i];                                       \
        rp->time = pasynUser->timestamp;                                                           \
        rp->status = pasynUser->auxStatus;                                                         \
        pPvt->ringHead = (pPvt->ringHead==pPvt->ringSize-1) ? 0 : pPvt->ringHead+1;                \
        if (pPvt->ringHead == pPvt->ringTail) {                                                    \
            /* There was no room in the ring buffer.  In the past we just threw away               \
             * the new value.  However, it is better to remove the oldest value from the           \
             * ring buffer and add the new one.  That way the final value the record receives      \
             * is guaranteed to be the most recent value */                                        \
            pPvt->ringTail = (pPvt->ringTail==pPvt->ringSize-1) ? 0 : pPvt->ringTail+1;            \
            pPvt->ringBufferOverflows++;                                                           \
        } else {                                                                                   \
            /* We only need to request the record to process if we added a new                     \
             * element to the ring buffer, not if we just replaced an element. */                  \
            if (pPvt->isOutput)                                                                    \
                scanOnce((dbCommon *)pwf);                                                         \
            else                                                                                   \
                scanIoRequest(pPvt->ioScanPvt);                                                    \
        }                                                                                          \
        epicsMutexUnlock(pPvt->ringBufferLock);                                                    \
    }                                                                                              \
}                                                                                                  \

