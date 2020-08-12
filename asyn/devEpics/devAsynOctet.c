/* devAsynOctet.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
    Author:  Marty Kraimer
    02SEP2004

    This file provides device support for stringin, stringout, lsi, lso, printf, scalcout and waveform.
    NOTE: waveform must be a array of chars
    asynSiOctetCmdResponse,asynWfOctetCmdResponse:
        INP has a command string.
        The command string is sent and a response read.
    asynSiOctetWriteRead,asynWfOctetWriteRead
        INP contains the name of a PV (string or array of chars)
        The value read from PV is sent and a respose read.
    asynSiOctetRead,asynWfOctetRead
        INP contains <drvUser> which is passed to asynDrvUser.create
        A response is read from the device.
    asynSoOctetWrite,asynWfOctetWrite
        INP contains <drvUser> which is passed to asynDrvUser.create
        VAL is sent
*/

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <alarm.h>
#include <recGbl.h>
#include <dbAccess.h>
#include <dbDefs.h>
#include <link.h>
#include <epicsPrint.h>
#include <epicsMutex.h>
#include <epicsString.h>
#include <cantProceed.h>
#include <dbCommon.h>
#include <dbScan.h>
#include <dbStaticLib.h>
#include <callback.h>
#include <stringinRecord.h>
#include <stringoutRecord.h>
#include <waveformRecord.h>
#ifdef HAVE_LSREC
#include <lsiRecord.h>
#include <lsoRecord.h>
#include <printfRecord.h>
#endif /* HAVE_LSREC */
#ifdef HAVE_CALCMOD
#include "sCalcoutRecord.h"
#endif /* HAVE_CALCMOD */
#include <menuFtype.h>
#include <recSup.h>
#include <devSup.h>

#include <epicsExport.h>
#include "asynDriver.h"
#include "asynDrvUser.h"
#include "asynOctet.h"
#include "asynOctetSyncIO.h"
#include "asynEpicsUtils.h"

#define INIT_OK 0
#define INIT_ERROR -1
#define DEFAULT_RING_BUFFER_SIZE 0

static const char *driverName = "devAsynOctet";

typedef struct ringBufferElement {
    char                *pValue;
    size_t              len;
    epicsTimeStamp      time;
    asynStatus          status;
    epicsAlarmCondition alarmStatus;
    epicsAlarmSeverity  alarmSeverity;
} ringBufferElement;


typedef struct devPvt {
    dbCommon            *precord;
    asynUser            *pasynUser;
    char                *portName;
    int                 addr;
    asynOctet           *poctet;
    void                *octetPvt;
    int                 canBlock;
    char                *userParam;
    int                 isOutput;
    int                 isWaveform;
    epicsUInt32         *pLen; /* pointer to string length field to update, nord for waveform, len for lsi/lso/printf */
    /* Following are for CmdResponse */
    char                *buffer;
    size_t              bufSize;
    size_t              bufLen;
    /* Following are for ring buffer support */
    epicsMutexId        devPvtLock;
    ringBufferElement   *ringBuffer;
    int                 ringHead;
    int                 ringTail;
    int                 ringSize;
    int                 ringBufferOverflows;
    ringBufferElement   result;
    char                *pValue;
    size_t              valSize;
    epicsUInt32         nord;
    /* Following for writeRead */
    DBADDR              dbAddr;
    /* Following are for I/O Intr*/
    CALLBACK            processCallback;
    CALLBACK            outputCallback;
    int                 newOutputCallbackValue;
    int                 numDeferredOutputCallbacks;
    int                 asyncProcessingActive;
    IOSCANPVT           ioScanPvt;
    void                *registrarPvt;
    int                 gotValue;
    interruptCallbackOctet interruptCallback;
    asynStatus          previousQueueRequestStatus;
} devPvt;

static long initCommon(dbCommon *precord, DBLINK *plink, userCallback callback,
                int isOutput, int isWaveform, int useDrvUser, char *pValue,
                epicsUInt32* pLen, size_t valSize);
static long createRingBuffer(dbCommon *pr, int minRingSize);
static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt);
static void outputCallbackCallback(CALLBACK *pcb);
static void interruptCallback(void *drvPvt, asynUser *pasynUser,
                char *value, size_t len, int eomReason);
static int initDrvUser(devPvt *pPvt);
static int initCmdBuffer(devPvt *pPvt);
static int initDbAddr(devPvt *pPvt);
static asynStatus writeIt(asynUser *pasynUser, const char *message,
                size_t nbytes);
static asynStatus readIt(asynUser *pasynUser, char *message,
                size_t maxBytes, size_t *nBytesRead);
static long processCommon(dbCommon *precord);
static void finish(dbCommon *precord);

static long initSiCmdResponse(stringinRecord *psi);
static void callbackSiCmdResponse(asynUser *pasynUser);
static long initSiWriteRead(stringinRecord *psi);
static void callbackSiWriteRead(asynUser *pasynUser);
static long initSiRead(stringinRecord *psi);
static void callbackSiRead(asynUser *pasynUser);
static long initSoWrite(stringoutRecord *pso);
static void callbackSoWrite(asynUser *pasynUser);

static long initWfCmdResponse(waveformRecord *pwf);
static void callbackWfCmdResponse(asynUser *pasynUser);
static long initWfWriteRead(waveformRecord *pwf);
static void callbackWfWriteRead(asynUser *pasynUser);
static long initWfRead(waveformRecord *pwf);
static void callbackWfRead(asynUser *pasynUser);
static long initWfWrite(waveformRecord *pwf);
static void callbackWfWrite(asynUser *pasynUser);
static long initWfWriteBinary(waveformRecord *pwf);
static void callbackWfWriteBinary(asynUser *pasynUser);

#ifdef HAVE_LSREC
static long initLsiCmdResponse(lsiRecord *plsi);
static void callbackLsiCmdResponse(asynUser *pasynUser);
static long initLsiWriteRead(lsiRecord *plsi);
static void callbackLsiWriteRead(asynUser *pasynUser);
static long initLsiRead(lsiRecord *plsi);
static void callbackLsiRead(asynUser *pasynUser);
static long initLsoWrite(lsoRecord *plso);
static void callbackLsoWrite(asynUser *pasynUser);

static long initPfWrite(printfRecord *ppf);
static void callbackPfWrite(asynUser *pasynUser);
#endif /* HAVE_LSREC */

#ifdef HAVE_CALCMOD
static long initScalcoutWrite(scalcoutRecord *pscalcout);
static void callbackScalcoutWrite(asynUser *pasynUser);
#endif /* HAVE_CALCMOD */

typedef struct commonDset {
    long          number;
    DEVSUPFUN     dev_report;
    DEVSUPFUN     init;
    DEVSUPFUN     init_record;
    DEVSUPFUN     get_ioint_info;
    DEVSUPFUN     processCommon;
} commonDset;

commonDset asynSiOctetCmdResponse = {
    5, 0, 0, initSiCmdResponse, 0,            processCommon};
commonDset asynSiOctetWriteRead   = {
    5, 0, 0, initSiWriteRead,   0,            processCommon};
commonDset asynSiOctetRead        = {
    5, 0, 0, initSiRead,        getIoIntInfo, processCommon};
commonDset asynSoOctetWrite       = {
    5, 0, 0, initSoWrite,       0,            processCommon};
commonDset asynWfOctetCmdResponse = {
    5, 0, 0, initWfCmdResponse, 0,            processCommon};
commonDset asynWfOctetWriteRead   = {
    5, 0, 0, initWfWriteRead,   0,            processCommon};
commonDset asynWfOctetRead        = {
    5, 0, 0, initWfRead,        getIoIntInfo, processCommon};
commonDset asynWfOctetWrite       = {
    5, 0, 0, initWfWrite,       0,            processCommon};
commonDset asynWfOctetWriteBinary = {
    5, 0, 0, initWfWriteBinary, 0,            processCommon};
#ifdef HAVE_LSREC
commonDset asynLsiOctetCmdResponse = {
    5, 0, 0, initLsiCmdResponse, 0,            processCommon};
commonDset asynLsiOctetWriteRead   = {
    5, 0, 0, initLsiWriteRead,   0,            processCommon};
commonDset asynLsiOctetRead        = {
    5, 0, 0, initLsiRead,        getIoIntInfo, processCommon};
commonDset asynLsoOctetWrite       = {
    5, 0, 0, initLsoWrite,       0,            processCommon};
commonDset asynPfOctetWrite       = {
    5, 0, 0, initPfWrite,       0,            processCommon};
#endif /* HAVE_LSREC */
#ifdef HAVE_CALCMOD
commonDset asynScalcoutOctetWrite       = {
    5, 0, 0, initScalcoutWrite,       0,            processCommon};
#endif /* HAVE_CALCMOD */

epicsExportAddress(dset, asynSiOctetCmdResponse);
epicsExportAddress(dset, asynSiOctetWriteRead);
epicsExportAddress(dset, asynSiOctetRead);
epicsExportAddress(dset, asynSoOctetWrite);
epicsExportAddress(dset, asynWfOctetCmdResponse);
epicsExportAddress(dset, asynWfOctetWriteRead);
epicsExportAddress(dset, asynWfOctetRead);
epicsExportAddress(dset, asynWfOctetWrite);
epicsExportAddress(dset, asynWfOctetWriteBinary);
#ifdef HAVE_LSREC
epicsExportAddress(dset, asynLsiOctetCmdResponse);
epicsExportAddress(dset, asynLsiOctetWriteRead);
epicsExportAddress(dset, asynLsiOctetRead);
epicsExportAddress(dset, asynLsoOctetWrite);
epicsExportAddress(dset, asynPfOctetWrite);
#endif /* HAVE_LSREC */
#ifdef HAVE_CALCMOD
epicsExportAddress(dset, asynScalcoutOctetWrite);
#endif /* HAVE_CALCMOD */

static long initCommon(dbCommon *precord, DBLINK *plink, userCallback callback,
                       int isOutput, int isWaveform, int useDrvUser, char *pValue,
                       epicsUInt32 *pLen, size_t valSize)
{
    devPvt        *pPvt;
    asynStatus    status;
    asynUser      *pasynUser;
    asynInterface *pasynInterface;
    commonDset    *pdset = (commonDset *)precord->dset;
    asynOctet     *poctet;
    char          *buffer;
    static const char *functionName="initCommon";

    pPvt = callocMustSucceed(1,sizeof(*pPvt),"devAsynOctet::initCommon");
    precord->dpvt = pPvt;
    pPvt->precord = precord;
    pPvt->isOutput = isOutput;
    pPvt->isWaveform = isWaveform;
    pPvt->pValue = pValue;
    pPvt->pLen = pLen;
    pPvt->valSize = valSize;
    pPvt->interruptCallback = interruptCallback;
    /* Create asynUser */
    pasynUser = pasynManager->createAsynUser(callback, 0);
    pasynUser->userPvt = pPvt;
    pPvt->pasynUser = pasynUser;
    status = pasynEpicsUtils->parseLink(pasynUser, plink,
                &pPvt->portName, &pPvt->addr,&pPvt->userParam);
    if (status != asynSuccess) {
        printf("%s %s::%s error in link %s\n",
                     precord->name, driverName, functionName, pasynUser->errorMessage);
        goto bad;
    }
    /* Connect to device */
    status = pasynManager->connectDevice(pasynUser,
        pPvt->portName, pPvt->addr);
    if (status != asynSuccess) {
        printf("%s %s::%s connectDevice failed %s\n",
                     precord->name, driverName, functionName, pasynUser->errorMessage);
        goto bad;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynOctetType,1);
    if(!pasynInterface) {
        printf("%s %s::%s interface %s not found\n",
            precord->name, driverName, functionName, asynOctetType);
        goto bad;
    }
    pPvt->poctet = poctet = pasynInterface->pinterface;
    pPvt->octetPvt = pasynInterface->drvPvt;
    /* Determine if device can block */
    pasynManager->canBlock(pasynUser, &pPvt->canBlock);
    if(pdset->get_ioint_info) {
        scanIoInit(&pPvt->ioScanPvt);
    }
    pPvt->devPvtLock = epicsMutexCreate();                                                     \
    /* If the drvUser interface should be used initialize it */
    if (useDrvUser) {
        if (initDrvUser(pPvt)) goto bad;
    }

    if (pPvt->isWaveform) {
        waveformRecord *pwf = (waveformRecord *)precord;
        if(pwf->ftvl!=menuFtypeCHAR && pwf->ftvl!=menuFtypeUCHAR) {
           printf("%s FTVL Must be CHAR or UCHAR\n",pwf->name);
           pwf->pact = 1;
           goto bad;
        }
    }
    if(valSize <= 0) {
       printf("%s record size must be > 0\n",precord->name);
       precord->pact = 1;
       goto bad;
    }

    /* If this is an output record
     *  - If the info field "asyn:INITIAL_READBACK" is 1 then try to read the initial value from the driver
     *  - If the info field "asyn:READBACK" is 1 then register for callbacks
    */
    if (pPvt->isOutput) {
        int enableReadbacks = 0;
        const char *readbackString;
        int enableInitialReadback = 0;
        const char *initialReadbackString;
        DBENTRY *pdbentry = dbAllocEntry(pdbbase);
        size_t nBytesRead;
        int eomReason;
        asynUser *pasynUserSync;

        status = dbFindRecord(pdbentry, precord->name);
        if (status) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s %s::%s error finding record\n",
                precord->name, driverName, functionName);
            goto bad;
        }
        readbackString = dbGetInfo(pdbentry, "asyn:READBACK");
        if (readbackString) enableReadbacks = atoi(readbackString);
        if (enableReadbacks) {
            /* If enableReabacks is set we will get a deadlock if not using ring buffer.
               Force ring buffer size to be at least 1. asyn:FIFO can be used to make it larger. */
            status = createRingBuffer(precord, 1);
            if (status != asynSuccess) goto bad;
            status = pPvt->poctet->registerInterruptUser(
               pPvt->octetPvt, pPvt->pasynUser,
               pPvt->interruptCallback, pPvt, &pPvt->registrarPvt);
            if(status != asynSuccess) {
                printf("%s %s::%s error calling registerInterruptUser %s\n",
                       precord->name, driverName, functionName, pPvt->pasynUser->errorMessage);
            }
            /* Initialize the interrupt callback */
            callbackSetCallback(outputCallbackCallback, &pPvt->outputCallback);
            callbackSetPriority(precord->prio, &pPvt->outputCallback);
            callbackSetUser(pPvt, &pPvt->outputCallback);
        }

        initialReadbackString = dbGetInfo(pdbentry, "asyn:INITIAL_READBACK");
        if (initialReadbackString) enableInitialReadback = atoi(initialReadbackString);
        if (enableInitialReadback) {
            /* Initialize synchronous interface */
            status = pasynOctetSyncIO->connect(pPvt->portName, pPvt->addr,
                         &pasynUserSync, pPvt->userParam);
            if (status != asynSuccess) {
                printf("%s %s::%s octetSyncIO->connect failed %s\n",
                       precord->name, driverName, functionName, pasynUserSync->errorMessage);
                goto bad;
            }
            buffer = malloc(valSize);
            status = pasynOctetSyncIO->read(pasynUserSync, buffer, valSize,
                                            pPvt->pasynUser->timeout, &nBytesRead, &eomReason);
            if (status == asynSuccess) {
                precord->udf = 0;
                if (nBytesRead == valSize) nBytesRead--;
                buffer[nBytesRead] = 0;
                strcpy(pValue, buffer);
                if (pPvt->pLen != NULL)
                {
                    *(pPvt->pLen) = (epicsUInt32)(pPvt->isWaveform ? nBytesRead : nBytesRead + 1); /* lsi, lso and printf count \0 in length */
                }
            }
            free(buffer);
            pasynOctetSyncIO->disconnect(pasynUserSync);
        }
    }

    return(INIT_OK);

bad:
    recGblSetSevr(precord,LINK_ALARM,INVALID_ALARM);
    precord->pact=1;
    return(INIT_ERROR);
}


static long createRingBuffer(dbCommon *pr, int minRingSize)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    asynStatus status;
    int i;
    const char *sizeString;
    static const char *functionName="createRingBuffer";

    if (!pPvt->ringBuffer) {
        DBENTRY *pdbentry = dbAllocEntry(pdbbase);
        status = dbFindRecord(pdbentry, pr->name);
        if (status) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s %s::%s error finding record\n",
                pr->name, driverName, functionName);
            return -1;
        }
        pPvt->ringSize = minRingSize;
        sizeString = dbGetInfo(pdbentry, "asyn:FIFO");
        if (sizeString) pPvt->ringSize = atoi(sizeString);
        if (pPvt->ringSize > 0) {
            pPvt->ringBuffer = callocMustSucceed(pPvt->ringSize+1, sizeof *pPvt->ringBuffer,
                                                "devAsynOctet::createRingBuffer");
            /* Allocate array for each ring buffer element */
            for (i=0; i<pPvt->ringSize+1; i++) {
                pPvt->ringBuffer[i].pValue = callocMustSucceed(pPvt->valSize, 1,
                        "devAsynOctet::createRingBuffer creating ring element array");
            }
        }
    }
    return asynSuccess;
}


static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    asynStatus status;
    static const char *functionName="getIoIntInfo";

    /* If initCommon failed then pPvt->poctet is NULL, return error */
    if (!pPvt->poctet) return -1;

    if (cmd == 0) {
        /* Add to scan list.  Register interrupts */
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s %s::%s registering interrupt\n",
            pr->name, driverName, functionName);
        createRingBuffer(pr, DEFAULT_RING_BUFFER_SIZE);
        status = pPvt->poctet->registerInterruptUser(
           pPvt->octetPvt,pPvt->pasynUser,
           pPvt->interruptCallback,pPvt,&pPvt->registrarPvt);
        if(status!=asynSuccess) {
            printf("%s %s::%s error calling registerInterruptUser %s\n",
                   pr->name, driverName, functionName, pPvt->pasynUser->errorMessage);
        }
    } else {
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s %s::%s cancelling interrupt\n",
             pr->name, driverName, functionName);
        status = pPvt->poctet->cancelInterruptUser(pPvt->octetPvt,
             pPvt->pasynUser,pPvt->registrarPvt);
        if(status!=asynSuccess) {
            printf("%s %s::%s error calling cancelInterruptUser %s\n",
                   pr->name, driverName, functionName, pPvt->pasynUser->errorMessage);
        }
    }
    *iopvt = pPvt->ioScanPvt;
    return 0;
}

static int getRingBufferValue(devPvt *pPvt)
{
    int ret = 0;
    static const char *functionName="getRingBufferValue";

    epicsMutexLock(pPvt->devPvtLock);
    if (pPvt->ringTail != pPvt->ringHead) {
        if (pPvt->ringBufferOverflows > 0) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_WARNING,
                "%s %s::%s warning, %d ring buffer overflows\n",
                pPvt->precord->name, driverName, functionName, pPvt->ringBufferOverflows);
            pPvt->ringBufferOverflows = 0;
        }
        pPvt->result = pPvt->ringBuffer[pPvt->ringTail];
        pPvt->ringTail = (pPvt->ringTail==pPvt->ringSize) ? 0 : pPvt->ringTail+1;
        ret = 1;
    }
    epicsMutexUnlock(pPvt->devPvtLock);
    return ret;
}

static void interruptCallback(void *drvPvt, asynUser *pasynUser,
                char *value, size_t len, int eomReason)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    dbCommon *pr = pPvt->precord;
    static const char *functionName="interruptCallback";

    epicsMutexLock(pPvt->devPvtLock);
    asynPrintIO(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        (char *)value, len*sizeof(char),
        "%s %s::%s ringSize=%d, len=%d, callback data:",
        pr->name, driverName, functionName, pPvt->ringSize, (int)len);
    if (len >= pPvt->valSize) len = pPvt->valSize-1;
    if (pPvt->ringSize == 0) {
        /* Not using a ring buffer */
        if (pasynUser->auxStatus == asynSuccess) {
            /* Note: calling dbScanLock here may to lead to deadlocks when asyn:READBACK is set for output records
             * and the driver is non-blocking.
             * This has been fixed in the asynInt32, asynFloat64, and asynUInt32Digital device support.
             * It cannot be fixed here because when not using ring buffers we need to lock the record to directly copy to it
             * Maybe we should require at least 1 ring buffer. */
            epicsMutexUnlock(pPvt->devPvtLock);
            dbScanLock(pr);
            memcpy(pPvt->pValue, value, len);
            dbScanUnlock(pr);
            epicsMutexLock(pPvt->devPvtLock);
            pPvt->pValue[len] = 0;
        }
        pPvt->nord = (epicsUInt32)len;
        pPvt->gotValue++;
        pPvt->result.status = pasynUser->auxStatus;
        pPvt->result.time = pasynUser->timestamp;
        pPvt->result.alarmStatus = pasynUser->alarmStatus;
        pPvt->result.alarmSeverity = pasynUser->alarmSeverity;
        if (pPvt->isOutput) {
            /* If this callback was received during asynchronous record processing
             * we must defer calling callbackRequest until end of record processing */
            if (pPvt->asyncProcessingActive) {
                pPvt->numDeferredOutputCallbacks++;
            } else {
                callbackRequest(&pPvt->outputCallback);
            }
        } else {
            scanIoRequest(pPvt->ioScanPvt);
        }
    } else {
        /* Using a ring buffer */
        ringBufferElement *rp;

        /* If interruptAccept is false we just return.  This prevents more ring pushes than pops.
         * There will then be nothing in the ring buffer, so the first
         * read will do a read from the driver, which should be OK. */
        if (!interruptAccept) {
            epicsMutexUnlock(pPvt->devPvtLock);
            return;
        }
        rp = &pPvt->ringBuffer[pPvt->ringHead];
        rp->len = len;
        memcpy(rp->pValue, value, len);
        rp->pValue[len] = 0;
        rp->time = pasynUser->timestamp;
        rp->status = pasynUser->auxStatus;
        rp->alarmStatus = pasynUser->alarmStatus;
        rp->alarmSeverity = pasynUser->alarmSeverity;
        pPvt->ringHead = (pPvt->ringHead==pPvt->ringSize) ? 0 : pPvt->ringHead+1;
        if (pPvt->ringHead == pPvt->ringTail) {
            /* There was no room in the ring buffer.  Remove the oldest value from the
             * ring buffer and add the new one so the final value the record receives
             * is guaranteed to be the most recent value */
            pPvt->ringTail = (pPvt->ringTail==pPvt->ringSize) ? 0 : pPvt->ringTail+1;
            pPvt->ringBufferOverflows++;
        } else {
            /* We only need to request the record to process if we added a new
             * element to the ring buffer, not if we just replaced an element. */
            if (pPvt->isOutput) {
                /* If this callback was received during asynchronous record processing
                 * we must defer calling callbackRequest until end of record processing */
                if (pPvt->asyncProcessingActive) {
                    pPvt->numDeferredOutputCallbacks++;
                } else {
                    callbackRequest(&pPvt->outputCallback);
                }
            } else {
                scanIoRequest(pPvt->ioScanPvt);
            }
        }
    }
    epicsMutexUnlock(pPvt->devPvtLock);
}

static void outputCallbackCallback(CALLBACK *pcb)
{
    devPvt *pPvt;
    static const char *functionName="outputCallbackCallback";

    callbackGetUser(pPvt, pcb);
    {
        dbCommon *pr = pPvt->precord;
        dbScanLock(pr);
        epicsMutexLock(pPvt->devPvtLock);
        pPvt->newOutputCallbackValue = 1;
        dbProcess(pr);
        if (pPvt->newOutputCallbackValue != 0) {
            /* We called dbProcess but the record did not process, perhaps because PACT was 1
             * Need to remove ring buffer element */
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s %s::%s warning dbProcess did not process record, PACT=%d\n",
                pr->name, driverName, functionName, pr->pact);
            if (pPvt->ringSize > 0) {
                getRingBufferValue(pPvt);
            }
            pPvt->newOutputCallbackValue = 0;
        }
        epicsMutexUnlock(pPvt->devPvtLock);
        dbScanUnlock(pr);
    }
}

static int initDrvUser(devPvt *pPvt)
{
    asynUser      *pasynUser = pPvt->pasynUser;
    asynStatus    status;
    asynInterface *pasynInterface;
    dbCommon      *precord = pPvt->precord;
    static const char *functionName="initDrvUser";

    /*call drvUserCreate*/
    pasynInterface = pasynManager->findInterface(pasynUser,asynDrvUserType,1);
    if(pasynInterface && pPvt->userParam) {
        asynDrvUser *pasynDrvUser;
        void       *drvPvt;

        pasynDrvUser = (asynDrvUser *)pasynInterface->pinterface;
        drvPvt = pasynInterface->drvPvt;
        status = pasynDrvUser->create(drvPvt,pasynUser,pPvt->userParam,0,0);
        if(status!=asynSuccess) {
            precord->pact=1;
            printf("%s %s::%s drvUserCreate failed %s\n",
                     precord->name, driverName, functionName, pasynUser->errorMessage);
            recGblSetSevr(precord,LINK_ALARM,INVALID_ALARM);
            return INIT_ERROR;
        }
    }
    return INIT_OK;
}

static int initCmdBuffer(devPvt *pPvt)
{
    size_t   len;
    dbCommon *precord = pPvt->precord;
    static const char *functionName="initCmdBuffer";

    len = strlen(pPvt->userParam);
    if(len<=0) {
        printf("%s  %s::%s no userParam\n",precord->name, driverName, functionName);
        precord->pact = 1;
        recGblSetSevr(precord,LINK_ALARM,INVALID_ALARM);
        return INIT_ERROR;
    }
    pPvt->buffer = callocMustSucceed(len,sizeof(char),"devAsynOctet::initCmdBuffer");
    dbTranslateEscape(pPvt->buffer,pPvt->userParam);
    pPvt->bufSize = len;
    pPvt->bufLen = strlen(pPvt->buffer);
    return INIT_OK;
}

static int initDbAddr(devPvt *pPvt)
{
    char      *userParam;
    dbCommon *precord = pPvt->precord;
    static const char *functionName="initDbAddr";

    userParam = pPvt->userParam;
    if(dbNameToAddr(userParam,&pPvt->dbAddr)) {
        printf("%s %s::%s record %s not present\n",
            precord->name, driverName, functionName, userParam);
        precord->pact = 1;
        recGblSetSevr(precord,LINK_ALARM,INVALID_ALARM);
        return INIT_ERROR;
    }
    return INIT_OK;
}

static asynStatus writeIt(asynUser *pasynUser,const char *message,size_t nbytes)
{
    devPvt     *pPvt = (devPvt *)pasynUser->userPvt;
    dbCommon   *precord = pPvt->precord;
    asynOctet  *poctet = pPvt->poctet;
    void       *octetPvt = pPvt->octetPvt;
    size_t     nbytesTransfered;
    static const char *functionName="writeIt";

    pPvt->result.status = poctet->write(octetPvt,pasynUser,message,nbytes,&nbytesTransfered);
    pPvt->result.time = pPvt->pasynUser->timestamp;
    pPvt->result.alarmStatus = pPvt->pasynUser->alarmStatus;
    pPvt->result.alarmSeverity = pPvt->pasynUser->alarmSeverity;
    if(pPvt->result.status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s %s::%s failed %s\n",
            precord->name, driverName, functionName, pasynUser->errorMessage);
        return pPvt->result.status;
    }
    if(nbytes != nbytesTransfered) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s %s::%s requested %lu but sent %lu bytes\n",
            precord->name, driverName, functionName, (unsigned long)nbytes, (unsigned long)nbytesTransfered);
        recGblSetSevr(precord, WRITE_ALARM, MINOR_ALARM);
        return asynError;
    }
    asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,message,nbytes,
       "%s %s::%s\n",precord->name, driverName, functionName);
    return pPvt->result.status;
}

static asynStatus readIt(asynUser *pasynUser,char *message,
        size_t maxBytes, size_t *nBytesRead)
{
    devPvt     *pPvt = (devPvt *)pasynUser->userPvt;
    dbCommon   *precord = pPvt->precord;
    asynOctet  *poctet = pPvt->poctet;
    void       *octetPvt = pPvt->octetPvt;
    int        eomReason;
    static const char *functionName="readIt";

    pPvt->result.status = poctet->read(octetPvt,pasynUser,message,maxBytes,
        nBytesRead,&eomReason);
    pPvt->result.time = pPvt->pasynUser->timestamp;
    pPvt->result.alarmStatus = pPvt->pasynUser->alarmStatus;
    pPvt->result.alarmSeverity = pPvt->pasynUser->alarmSeverity;
    if(pPvt->result.status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s %s::%s failed %s\n",
            precord->name, driverName, functionName, pasynUser->errorMessage);
        return pPvt->result.status;
    }
    asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,message,*nBytesRead,
       "%s %s::%s eomReason %d\n",precord->name, driverName, functionName, eomReason);
    return pPvt->result.status;
}

static void reportQueueRequestStatus(devPvt *pPvt, asynStatus status)
{
    static const char *functionName="reportQueueRequestStatus";

    if (pPvt->previousQueueRequestStatus != status) {
        pPvt->previousQueueRequestStatus = status;
        if (status == asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s %s::%s queueRequest status returned to normal\n",
                pPvt->precord->name, driverName, functionName);
        } else {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s %s::%s queueRequest error %s\n",
                pPvt->precord->name, driverName, functionName,pPvt->pasynUser->errorMessage);
        }
    }
}
static long processCommon(dbCommon *precord)
{
    devPvt *pPvt = (devPvt *)precord->dpvt;
    int gotCallbackData;
    asynStatus status;
    static const char *functionName="processCommon";

    epicsMutexLock(pPvt->devPvtLock);
    if (pPvt->isOutput) {
        if (pPvt->ringSize == 0) {
            gotCallbackData = pPvt->newOutputCallbackValue;
        } else {
            gotCallbackData = pPvt->newOutputCallbackValue && getRingBufferValue(pPvt);
        }
    } else {
        if (pPvt->ringSize == 0) {
            gotCallbackData = pPvt->gotValue;
        } else {
            gotCallbackData = getRingBufferValue(pPvt);
        }
    }

    if (!gotCallbackData && precord->pact == 0) {
        if(pPvt->canBlock) {
            precord->pact = 1;
            pPvt->asyncProcessingActive = 1;
        }
        epicsMutexUnlock(pPvt->devPvtLock);
        status = pasynManager->queueRequest(pPvt->pasynUser, asynQueuePriorityMedium, 0);
        if((status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) precord->pact = 0;
        epicsMutexLock(pPvt->devPvtLock);
        reportQueueRequestStatus(pPvt, status);

    }
    if (gotCallbackData) {
        int len;
        if (pPvt->ringSize == 0) {
            /* Data has already been copied to the record in interruptCallback */
            pPvt->gotValue--;
            if ((pPvt->pLen != NULL) && (pPvt->result.status == asynSuccess)) {
                (*pPvt->pLen) = (pPvt->isWaveform ? pPvt->nord : pPvt->nord + 1); /* lsi, lso and printf count \0 in length */
            }
            if (pPvt->gotValue) {
                asynPrint(pPvt->pasynUser, ASYN_TRACE_WARNING,
                    "%s %s::%s warning: multiple interrupt callbacks between processing\n",
                     precord->name, driverName, functionName);
            }
        } else {
            /* Copy data from ring buffer */
            ringBufferElement *rp = &pPvt->result;
            /* Need to copy the array with the lock because that is shared even though
               pPvt->result is a copy */
            epicsMutexLock(pPvt->devPvtLock);
            if (rp->status == asynSuccess) {
                memcpy(pPvt->pValue, rp->pValue, rp->len);
                if (pPvt->pLen != NULL) {
                    (*pPvt->pLen) = (epicsUInt32)(pPvt->isWaveform ? rp->len : rp->len + 1); /* lsi, lso and printf count \0 in length */
                }
            }
            precord->time = rp->time;
            epicsMutexUnlock(pPvt->devPvtLock);
        }
        len = (int)strlen(pPvt->pValue);
        asynPrintIO(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
            pPvt->pValue, len,
            "%s %s::%s len=%d,  data:",
            precord->name, driverName, functionName, len);
    }

    pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status,
                                            pPvt->isOutput ? WRITE_ALARM : READ_ALARM, &pPvt->result.alarmStatus,
                                            INVALID_ALARM, &pPvt->result.alarmSeverity);
    (void)recGblSetSevr(precord, pPvt->result.alarmStatus, pPvt->result.alarmSeverity);
    if (pPvt->numDeferredOutputCallbacks > 0) {
        callbackRequest(&pPvt->outputCallback);
        pPvt->numDeferredOutputCallbacks--;
    }
    pPvt->newOutputCallbackValue = 0;
    pPvt->asyncProcessingActive = 0;
    epicsMutexUnlock(pPvt->devPvtLock);
    if (pPvt->result.status == asynSuccess) {
        pPvt->precord->udf = 0;
        return 0;
    } else {
        pPvt->result.status = asynSuccess;
        return -1;
    }
}

static void finish(dbCommon *pr)
{
    devPvt     *pPvt = (devPvt *)pr->dpvt;

    if(pr->pact) callbackRequestProcessCallback(&pPvt->processCallback,pr->prio,pr);
}

static long initSiCmdResponse(stringinRecord *psi)
{
    devPvt     *pPvt;
    int        status;

    status = initCommon((dbCommon *)psi, &psi->inp, callbackSiCmdResponse,
                        0, 0, 0, psi->val, NULL, sizeof(psi->val));
    if(status!=INIT_OK) return status;
    pPvt = (devPvt *)psi->dpvt;
    return initCmdBuffer(pPvt);
}

static void callbackSiCmdResponse(asynUser *pasynUser)
{
    devPvt         *pPvt = (devPvt *)pasynUser->userPvt;
    stringinRecord *psi = (stringinRecord *)pPvt->precord;
    asynStatus     status;
    size_t         len = sizeof(psi->val);
    size_t         nBytesRead;

    status = writeIt(pasynUser,pPvt->buffer,pPvt->bufLen);
    if(status==asynSuccess) {
        status = readIt(pasynUser,psi->val,len,&nBytesRead);
        psi->time = pasynUser->timestamp;
        if(status==asynSuccess) {
            psi->udf = 0;
            if(nBytesRead==len) nBytesRead--;
            psi->val[nBytesRead] = 0;
        }
    }
    finish((dbCommon *)psi);
}

static long initSiWriteRead(stringinRecord *psi)
{
    int        status;
    devPvt     *pPvt;

    status = initCommon((dbCommon *)psi, &psi->inp, callbackSiWriteRead,
                        0, 0, 0, psi->val, NULL, sizeof(psi->val));
    if(status!=INIT_OK) return status;
    pPvt = (devPvt *)psi->dpvt;
    return initDbAddr(pPvt);
}

static void callbackSiWriteRead(asynUser *pasynUser)
{
    devPvt         *pPvt = (devPvt *)pasynUser->userPvt;
    stringinRecord *psi = (stringinRecord *)pPvt->precord;
    asynStatus     status;
    size_t         nBytesRead;
    long           dbStatus;
    char           raw[MAX_STRING_SIZE+1];
    char           translate[MAX_STRING_SIZE+1];
    size_t         len = sizeof(psi->val);

    dbStatus = dbGet(&pPvt->dbAddr,DBR_STRING,raw,0,0,0);
    raw[MAX_STRING_SIZE] = 0;
    if(dbStatus) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s dbGet failed\n",psi->name);
        recGblSetSevr(psi,READ_ALARM,INVALID_ALARM);
        finish((dbCommon *)psi);
        return;
    }
    dbTranslateEscape(translate,raw);
    status = writeIt(pasynUser,translate,strlen(translate));
    if(status==asynSuccess) {
        status = readIt(pasynUser,psi->val,len,&nBytesRead);
        psi->time = pasynUser->timestamp;
        if(status==asynSuccess) {
            psi->udf = 0;
            if(nBytesRead==len) nBytesRead--;
            psi->val[nBytesRead] = 0;
        }
    }
    finish((dbCommon *)psi);
}

static long initSiRead(stringinRecord *psi)
{
    return initCommon((dbCommon *)psi, &psi->inp, callbackSiRead,
                      0, 0, 1, psi->val, NULL, sizeof(psi->val));
}

static void callbackSiRead(asynUser *pasynUser)
{
    devPvt         *pPvt = (devPvt *)pasynUser->userPvt;
    stringinRecord *psi = (stringinRecord *)pPvt->precord;
    size_t         nBytesRead;
    asynStatus     status;
    size_t         len = sizeof(psi->val);

    status = readIt(pasynUser,psi->val,len,&nBytesRead);
    psi->time = pasynUser->timestamp;
    if (status==asynSuccess) {
        psi->udf = 0;
        if (nBytesRead==len) nBytesRead--;
        psi->val[nBytesRead] = 0;
    }
    finish((dbCommon *)psi);
}

static long initSoWrite(stringoutRecord *pso)
{
    return initCommon((dbCommon *)pso, &pso->out, callbackSoWrite,
                      1, 0, 1, pso->val, NULL, sizeof(pso->val));
}

/* implementation of strnlen() as i'm not sure it is available everywhere */
static size_t my_strnlen(const char *str, size_t max_size)
{
    const char * end = (const char *)memchr(str, '\0', max_size);
    if (end == NULL) {
        return max_size;
    } else {
        return end - str;
    }
}

static void callbackSoWrite(asynUser *pasynUser)
{
    devPvt          *pPvt = (devPvt *)pasynUser->userPvt;
    stringoutRecord *pso = (stringoutRecord *)pPvt->precord;

    writeIt(pasynUser, pso->val, my_strnlen(pso->val, sizeof(pso->val)));
    finish((dbCommon *)pso);
}

static long initWfCmdResponse(waveformRecord *pwf)
{
    int        status;

    status = initCommon((dbCommon *)pwf, &pwf->inp, callbackWfCmdResponse,
                        0, 1, 0, pwf->bptr, &(pwf->nord), pwf->nelm);
    if (status != INIT_OK) return status;
    return initCmdBuffer((devPvt *)pwf->dpvt);
}

static void callbackWfCmdResponse(asynUser *pasynUser)
{
    devPvt         *pPvt = (devPvt *)pasynUser->userPvt;
    waveformRecord *pwf = (waveformRecord *)pPvt->precord;
    asynStatus     status;
    size_t         nBytesRead;
    char           *pbuf = (char *)pwf->bptr;

    status = writeIt(pasynUser,pPvt->buffer,pPvt->bufLen);
    if(status==asynSuccess) {
        status = readIt(pasynUser,pwf->bptr,(size_t)pwf->nelm,&nBytesRead);
        pwf->time = pasynUser->timestamp;
        if(status==asynSuccess) {
            if (nBytesRead == pwf->nelm) nBytesRead--;
            pbuf[nBytesRead] = 0;
            pwf->udf = 0;
            pwf->nord = (epicsUInt32)nBytesRead;
        }
    }
    finish((dbCommon *)pwf);
}

static long initWfWriteRead(waveformRecord *pwf)
{
    int        status;

    status = initCommon((dbCommon *)pwf, &pwf->inp, callbackWfWriteRead,
                        0, 1, 0, pwf->bptr, &(pwf->nord), pwf->nelm);
    if (status != INIT_OK) return status;
    return initDbAddr((devPvt *)pwf->dpvt);
}

static void callbackWfWriteRead(asynUser *pasynUser)
{
    devPvt         *pPvt = (devPvt *)pasynUser->userPvt;
    waveformRecord *pwf = (waveformRecord *)pPvt->precord;
    asynStatus     status;
    size_t         nBytesRead;
    long           dbStatus;
    char           raw[MAX_STRING_SIZE+1];
    char           translate[MAX_STRING_SIZE+1];
    char           *pbuf = (char *)pwf->bptr;
    static const char *functionName="callbackWfWriteRead";

    dbStatus = dbGet(&pPvt->dbAddr, DBR_STRING, raw, 0, 0, 0);
    raw[MAX_STRING_SIZE] = 0;
    if(dbStatus) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s %s::%s dbGet failed\n",pwf->name, driverName, functionName);
        recGblSetSevr(pwf,READ_ALARM,INVALID_ALARM);
        finish((dbCommon *)pwf);
        return;
    }
    dbTranslateEscape(translate,raw);
    status = writeIt(pasynUser, translate, strlen(translate));
    if (status==asynSuccess) {
        status = readIt(pasynUser,pwf->bptr, (size_t)pwf->nelm, &nBytesRead);
        pwf->time = pasynUser->timestamp;
        if(status==asynSuccess) {
            if (nBytesRead == pwf->nelm) nBytesRead--;
            pbuf[nBytesRead] = 0;
            pwf->udf = 0;
            pwf->nord = (epicsUInt32)nBytesRead;
        }
    }
    finish((dbCommon *)pwf);
}

static long initWfRead(waveformRecord *pwf)
{
    return initCommon((dbCommon *)pwf, &pwf->inp, callbackWfRead,
                      0, 1, 1, pwf->bptr, &(pwf->nord), pwf->nelm);
}

static void callbackWfRead(asynUser *pasynUser)
{
    devPvt         *pPvt = (devPvt *)pasynUser->userPvt;
    waveformRecord *pwf = (waveformRecord *)pPvt->precord;
    size_t         nBytesRead;
    asynStatus     status;
    char           *pbuf = (char *)pwf->bptr;

    status = readIt(pasynUser, pwf->bptr, pwf->nelm, &nBytesRead);
    pwf->time = pasynUser->timestamp;
    if(status==asynSuccess) {
        if (nBytesRead == pwf->nelm) nBytesRead--;
        pbuf[nBytesRead] = 0;
        pwf->udf = 0;
        pwf->nord = (epicsUInt32)nBytesRead;
    }
    finish((dbCommon *)pwf);
}

static long initWfWrite(waveformRecord *pwf)
{
    return initCommon((dbCommon *)pwf, &pwf->inp, callbackWfWrite,
                      1, 1, 1, pwf->bptr, &(pwf->nord), pwf->nelm);
}

static void callbackWfWrite(asynUser *pasynUser)
{
    devPvt          *pPvt = (devPvt *)pasynUser->userPvt;
    waveformRecord  *pwf = (waveformRecord *)pPvt->precord;

    writeIt(pasynUser, pwf->bptr, my_strnlen(pwf->bptr, pwf->nord));
    finish((dbCommon *)pwf);
}

static long initWfWriteBinary(waveformRecord *pwf)
{
    return initCommon((dbCommon *)pwf, &pwf->inp, callbackWfWriteBinary,
                      1, 1, 1, pwf->bptr, &(pwf->nord), pwf->nelm);
}

static void callbackWfWriteBinary(asynUser *pasynUser)
{
    devPvt          *pPvt = (devPvt *)pasynUser->userPvt;
    waveformRecord  *pwf = (waveformRecord *)pPvt->precord;

    writeIt(pasynUser, pwf->bptr, pwf->nord);
    finish((dbCommon *)pwf);
}

#ifdef HAVE_LSREC

static long initLsiCmdResponse(lsiRecord *plsi)
{
    devPvt     *pPvt;
    int        status;

    status = initCommon((dbCommon *)plsi, &plsi->inp, callbackLsiCmdResponse,
                        0, 0, 0, plsi->val, &(plsi->len), plsi->sizv);
    if(status!=INIT_OK) return status;
    pPvt = (devPvt *)plsi->dpvt;
    return initCmdBuffer(pPvt);
}

static void callbackLsiCmdResponse(asynUser *pasynUser)
{
    devPvt         *pPvt = (devPvt *)pasynUser->userPvt;
    lsiRecord *plsi = (lsiRecord *)pPvt->precord;
    asynStatus     status;
    size_t         len = plsi->sizv;
    size_t         nBytesRead;

    status = writeIt(pasynUser,pPvt->buffer,pPvt->bufLen);
    if(status==asynSuccess) {
        status = readIt(pasynUser,plsi->val,len,&nBytesRead);
        plsi->time = pasynUser->timestamp;
        if(status==asynSuccess) {
            plsi->udf = 0;
            if(nBytesRead==len) nBytesRead--;
            plsi->val[nBytesRead] = 0;
            plsi->len = (epicsUInt32)nBytesRead + 1;
        }
    }
    finish((dbCommon *)plsi);
}

static long initLsiWriteRead(lsiRecord *plsi)
{
    int        status;
    devPvt     *pPvt;

    status = initCommon((dbCommon *)plsi, &plsi->inp, callbackLsiWriteRead,
                        0, 0, 0, plsi->val, &(plsi->len), plsi->sizv);
    if(status!=INIT_OK) return status;
    pPvt = (devPvt *)plsi->dpvt;
    return initDbAddr(pPvt);
}

static void callbackLsiWriteRead(asynUser *pasynUser)
{
    devPvt         *pPvt = (devPvt *)pasynUser->userPvt;
    lsiRecord *plsi = (lsiRecord *)pPvt->precord;
    asynStatus     status;
    size_t         nBytesRead;
    long           dbStatus;
    char           raw[MAX_STRING_SIZE+1];
    char           translate[MAX_STRING_SIZE+1];
    size_t         len = plsi->sizv;

    dbStatus = dbGet(&pPvt->dbAddr,DBR_STRING,raw,0,0,0);
    raw[MAX_STRING_SIZE] = 0;
    if(dbStatus) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s dbGet failed\n",plsi->name);
        recGblSetSevr(plsi,READ_ALARM,INVALID_ALARM);
        finish((dbCommon *)plsi);
        return;
    }
    dbTranslateEscape(translate,raw);
    status = writeIt(pasynUser,translate,strlen(translate));
    if(status==asynSuccess) {
        status = readIt(pasynUser,plsi->val,len,&nBytesRead);
        plsi->time = pasynUser->timestamp;
        if(status==asynSuccess) {
            plsi->udf = 0;
            if(nBytesRead==len) nBytesRead--;
            plsi->val[nBytesRead] = 0;
            plsi->len = (epicsUInt32)nBytesRead + 1;
        }
    }
    finish((dbCommon *)plsi);
}

static long initLsiRead(lsiRecord *plsi)
{
    return initCommon((dbCommon *)plsi, &plsi->inp, callbackLsiRead,
                      0, 0, 1, plsi->val, &(plsi->len), plsi->sizv);
}

static void callbackLsiRead(asynUser *pasynUser)
{
    devPvt         *pPvt = (devPvt *)pasynUser->userPvt;
    lsiRecord *plsi = (lsiRecord *)pPvt->precord;
    size_t         nBytesRead;
    asynStatus     status;
    size_t         len = plsi->sizv;

    status = readIt(pasynUser,plsi->val,len,&nBytesRead);
    plsi->time = pasynUser->timestamp;
    if (status==asynSuccess) {
        plsi->udf = 0;
        if (nBytesRead==len) nBytesRead--;
        plsi->val[nBytesRead] = 0;
        plsi->len = (epicsUInt32)nBytesRead + 1;
    }
    finish((dbCommon *)plsi);
}

static long initLsoWrite(lsoRecord *plso)
{
    return initCommon((dbCommon *)plso, &plso->out, callbackLsoWrite,
                      1, 0, 1, plso->val, &(plso->len), plso->sizv);
}

static void callbackLsoWrite(asynUser *pasynUser)
{
    devPvt          *pPvt = (devPvt *)pasynUser->userPvt;
    lsoRecord *plso = (lsoRecord *)pPvt->precord;

    writeIt(pasynUser, plso->val, my_strnlen(plso->val, plso->len));
    finish((dbCommon *)plso);
}

static long initPfWrite(printfRecord *ppf)
{
    return initCommon((dbCommon *)ppf, &ppf->out, callbackPfWrite,
                      1, 0, 1, ppf->val, &(ppf->len), ppf->sizv);
}

static void callbackPfWrite(asynUser *pasynUser)
{
    devPvt          *pPvt = (devPvt *)pasynUser->userPvt;
    printfRecord *ppf = (printfRecord *)pPvt->precord;

    writeIt(pasynUser, ppf->val, my_strnlen(ppf->val, ppf->len));
    finish((dbCommon *)ppf);
}

#endif /* HAVE_LSREC */

#ifdef HAVE_CALCMOD
static long initScalcoutWrite(scalcoutRecord *pscalcout)
{
    long ret;
    pscalcout->osv[0] = 0;
    ret = initCommon((dbCommon *)pscalcout, &pscalcout->out, callbackScalcoutWrite,
                      1, 0, 1, pscalcout->osv, NULL, sizeof(pscalcout->osv));
    /* update sval and val if an inital readback from the device */
    if (ret == INIT_OK && my_strnlen(pscalcout->osv, sizeof(pscalcout->osv)) > 0) {
        strncpy(pscalcout->sval, pscalcout->osv, sizeof(pscalcout->sval));
        pscalcout->sval[sizeof(pscalcout->sval) - 1] = 0;
        pscalcout->val = atof(pscalcout->sval);
    }
    return ret;
}

static void callbackScalcoutWrite(asynUser *pasynUser)
{
    devPvt          *pPvt = (devPvt *)pasynUser->userPvt;
    scalcoutRecord *pscalcout = (scalcoutRecord *)pPvt->precord;

    writeIt(pasynUser, pscalcout->osv, my_strnlen(pscalcout->osv, sizeof(pscalcout->osv)));
    finish((dbCommon *)pscalcout);
}
#endif /* HAVE_CALCMOD */
