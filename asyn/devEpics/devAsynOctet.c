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

    This file provides device support for stringin, stringout, and waveform.
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
    /* Following are for CmdResponse */
    char                *buffer;
    size_t              bufSize;
    size_t              bufLen;
    /* Following are for ring buffer support */
    epicsMutexId        ringBufferLock;
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
    CALLBACK            callback;
    IOSCANPVT           ioScanPvt;
    void                *registrarPvt;
    int                 gotValue;
    interruptCallbackOctet interruptCallback;
    asynStatus          previousQueueRequestStatus;
} devPvt;

static long initCommon(dbCommon *precord, DBLINK *plink, userCallback callback, 
                int isOutput, int isWaveform, int useDrvUser, char *pValue, size_t valSize);
static long createRingBuffer(dbCommon *pr);
static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt);
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

epicsExportAddress(dset, asynSiOctetCmdResponse);
epicsExportAddress(dset, asynSiOctetWriteRead);
epicsExportAddress(dset, asynSiOctetRead);
epicsExportAddress(dset, asynSoOctetWrite);
epicsExportAddress(dset, asynWfOctetCmdResponse);
epicsExportAddress(dset, asynWfOctetWriteRead);
epicsExportAddress(dset, asynWfOctetRead);
epicsExportAddress(dset, asynWfOctetWrite);
epicsExportAddress(dset, asynWfOctetWriteBinary);

static long initCommon(dbCommon *precord, DBLINK *plink, userCallback callback, 
                       int isOutput, int isWaveform, int useDrvUser, char *pValue, size_t valSize)
{
    devPvt        *pPvt;
    asynStatus    status;
    asynUser      *pasynUser;
    asynInterface *pasynInterface;
    commonDset    *pdset = (commonDset *)precord->dset;
    asynOctet     *poctet;
    char          *buffer;
    waveformRecord *pwf = (waveformRecord *)precord;

    pPvt = callocMustSucceed(1,sizeof(*pPvt),"devAsynOctet::initCommon");
    precord->dpvt = pPvt;
    pPvt->precord = precord;
    pPvt->isOutput = isOutput;
    pPvt->isWaveform = isWaveform;
    pPvt->pValue = pValue;
    pPvt->valSize = valSize;
    pPvt->interruptCallback = interruptCallback;
    /* Create asynUser */
    pasynUser = pasynManager->createAsynUser(callback, 0);
    pasynUser->userPvt = pPvt;
    pPvt->pasynUser = pasynUser;
    status = pasynEpicsUtils->parseLink(pasynUser, plink, 
                &pPvt->portName, &pPvt->addr,&pPvt->userParam);
    if (status != asynSuccess) {
        printf("%s %s::initCommon error in link %s\n",
                     precord->name, driverName, pasynUser->errorMessage);
        goto bad;
    }
    /* Connect to device */
    status = pasynManager->connectDevice(pasynUser,
        pPvt->portName, pPvt->addr);
    if (status != asynSuccess) {
        printf("%s %s::initCommon connectDevice failed %s\n",
                     precord->name, driverName, pasynUser->errorMessage);
        goto bad;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynOctetType,1);
    if(!pasynInterface) {
        printf("%s %s::initCommon interface %s not found\n",
            precord->name, driverName, asynOctetType);
        goto bad;
    }
    pPvt->poctet = poctet = pasynInterface->pinterface;
    pPvt->octetPvt = pasynInterface->drvPvt;
    /* Determine if device can block */
    pasynManager->canBlock(pasynUser, &pPvt->canBlock);
    if(pdset->get_ioint_info) {
        scanIoInit(&pPvt->ioScanPvt);
    }
    pPvt->ringBufferLock = epicsMutexCreate();                                                     \
    /* If the drvUser interface should be used initialize it */
    if (useDrvUser) {
        if (initDrvUser(pPvt)) goto bad;
    }

    if (pPvt->isWaveform) {
        if(pwf->ftvl!=menuFtypeCHAR && pwf->ftvl!=menuFtypeUCHAR) {
           printf("%s FTVL Must be CHAR or UCHAR\n",pwf->name);
           pwf->pact = 1;
           goto bad;
        } 
        if(pwf->nelm<=0) {
           printf("%s NELM must be > 0\n",pwf->name);
           pwf->pact = 1;
           goto bad;
        }
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
                "%s devAsynOctet::initCommon error finding record\n",
                precord->name);
            goto bad;
        }
        readbackString = dbGetInfo(pdbentry, "asyn:READBACK");
        if (readbackString) enableReadbacks = atoi(readbackString);
        if (enableReadbacks) {
            status = createRingBuffer(precord);
            if (status != asynSuccess) goto bad;
            status = pPvt->poctet->registerInterruptUser(
               pPvt->octetPvt, pPvt->pasynUser,
               pPvt->interruptCallback, pPvt, &pPvt->registrarPvt);
            if(status != asynSuccess) {
                printf("%s devAsynOctet::initCommon error calling registerInterruptUser %s\n",
                       precord->name, pPvt->pasynUser->errorMessage);
            }
        }

        initialReadbackString = dbGetInfo(pdbentry, "asyn:INITIAL_READBACK");
        if (initialReadbackString) enableInitialReadback = atoi(initialReadbackString);
        if (enableInitialReadback) {
            /* Initialize synchronous interface */
            status = pasynOctetSyncIO->connect(pPvt->portName, pPvt->addr, 
                         &pasynUserSync, pPvt->userParam);
            if (status != asynSuccess) {
                printf("%s devAsynOctet::initCommon octetSyncIO->connect failed %s\n",
                       precord->name, pasynUserSync->errorMessage);
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
                if (pPvt->isWaveform) pwf->nord = nBytesRead;
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


static long createRingBuffer(dbCommon *pr)
{
    devPvt *pPvt = (devPvt *)pr->dpvt;
    asynStatus status;
    int i;
    const char *sizeString;
    
    if (!pPvt->ringBuffer) {
        DBENTRY *pdbentry = dbAllocEntry(pdbbase);
        status = dbFindRecord(pdbentry, pr->name);
        if (status) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s %s::createRingBufffer error finding record\n",
                pr->name, driverName);
            return -1;
        }
        pPvt->ringSize = DEFAULT_RING_BUFFER_SIZE;
        sizeString = dbGetInfo(pdbentry, "asyn:FIFO");
        if (sizeString) pPvt->ringSize = atoi(sizeString);
        if (pPvt->ringSize > 0) {
            pPvt->ringBuffer = callocMustSucceed(pPvt->ringSize+1, sizeof *pPvt->ringBuffer, 
                                                "devAsynOctet::createRingBuffer");
            /* Allocate array for each ring buffer element */
            for (i=0; i<pPvt->ringSize; i++) {
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

    /* If initCommon failed then pPvt->poctet is NULL, return error */
    if (!pPvt->poctet) return -1;

    if (cmd == 0) {
        /* Add to scan list.  Register interrupts */
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s %s::getIoIntInfo registering interrupt\n",
            pr->name, driverName);
        createRingBuffer(pr);
        status = pPvt->poctet->registerInterruptUser(
           pPvt->octetPvt,pPvt->pasynUser,
           pPvt->interruptCallback,pPvt,&pPvt->registrarPvt);
        if(status!=asynSuccess) {
            printf("%s %s::getIoIntInfo error calling registerInterruptUser %s\n",
                   pr->name, driverName, pPvt->pasynUser->errorMessage);
        }
    } else {
        asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s %s::getIoIntInfo cancelling interrupt\n",
             pr->name, driverName);
        status = pPvt->poctet->cancelInterruptUser(pPvt->octetPvt,
             pPvt->pasynUser,pPvt->registrarPvt);
        if(status!=asynSuccess) {
            printf("%s %s::getIoIntInfo error calling cancelInterruptUser %s\n",
                   pr->name, driverName, pPvt->pasynUser->errorMessage);
        }
    }
    *iopvt = pPvt->ioScanPvt;
    return 0;
}

static int getRingBufferValue(devPvt *pPvt)
{
    int ret = 0;
    epicsMutexLock(pPvt->ringBufferLock);
    if (pPvt->ringTail != pPvt->ringHead) {
        if (pPvt->ringBufferOverflows > 0) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_WARNING,
                "%s %s::getRingBufferValue error, %d ring buffer overflows\n",
                pPvt->precord->name, driverName, pPvt->ringBufferOverflows);
            pPvt->ringBufferOverflows = 0;
        }
        pPvt->result = pPvt->ringBuffer[pPvt->ringTail]; 
        pPvt->ringTail = (pPvt->ringTail==pPvt->ringSize-1) ? 0 : pPvt->ringTail+1;
        ret = 1;
    }
    epicsMutexUnlock(pPvt->ringBufferLock);
    return ret;
}

static void interruptCallback(void *drvPvt, asynUser *pasynUser,
                char *value, size_t len, int eomReason)
{
    devPvt *pPvt = (devPvt *)drvPvt;
    dbCommon *pr = pPvt->precord;

    asynPrintIO(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        (char *)value, len*sizeof(char),
        "%s %s::interruptCallbackInput ringSize=%d, len=%d, callback data:",
        pr->name, driverName, pPvt->ringSize, (int)len);
    if (len >= pPvt->valSize) len = pPvt->valSize-1;
    if (pPvt->ringSize == 0) {
        /* Not using a ring buffer */ 
        dbScanLock(pr);
        pr->time = pasynUser->timestamp;
        if (pasynUser->auxStatus == asynSuccess) {
            memcpy(pPvt->pValue, value, len);
            pPvt->pValue[len] = 0;
        }
        pPvt->nord = (epicsUInt32)len;
        pPvt->gotValue++;
        pPvt->result.status = pasynUser->auxStatus;
        pPvt->result.time = pasynUser->timestamp;
        pPvt->result.alarmStatus = pasynUser->alarmStatus;
        pPvt->result.alarmSeverity = pasynUser->alarmSeverity;
        dbScanUnlock(pPvt->precord);
        if (pPvt->isOutput) 
            scanOnce(pPvt->precord);
        else
            scanIoRequest(pPvt->ioScanPvt);
    } else {
        /* Using a ring buffer */
        ringBufferElement *rp;

        /* If interruptAccept is false we just return.  This prevents more ring pushes than pops.
         * There will then be nothing in the ring buffer, so the first
         * read will do a read from the driver, which should be OK. */
        if (!interruptAccept) return;

        epicsMutexLock(pPvt->ringBufferLock);
        rp = &pPvt->ringBuffer[pPvt->ringHead];
        rp->len = len;
        memcpy(rp->pValue, value, len);        
        rp->pValue[len] = 0;
        rp->time = pasynUser->timestamp;
        rp->status = pasynUser->auxStatus;
        rp->alarmStatus = pasynUser->alarmStatus;
        rp->alarmSeverity = pasynUser->alarmSeverity;
        pPvt->ringHead = (pPvt->ringHead==pPvt->ringSize-1) ? 0 : pPvt->ringHead+1;
        if (pPvt->ringHead == pPvt->ringTail) {
            /* There was no room in the ring buffer.  Remove the oldest value from the
             * ring buffer and add the new one so the final value the record receives
             * is guaranteed to be the most recent value */
            pPvt->ringTail = (pPvt->ringTail==pPvt->ringSize-1) ? 0 : pPvt->ringTail+1;
            pPvt->ringBufferOverflows++;
        } else {
            /* We only need to request the record to process if we added a new
             * element to the ring buffer, not if we just replaced an element. */
            if (pPvt->isOutput) 
                scanOnce(pPvt->precord);
            else
                scanIoRequest(pPvt->ioScanPvt);
        }
        epicsMutexUnlock(pPvt->ringBufferLock);
    }
}


static int initDrvUser(devPvt *pPvt)
{
    asynUser      *pasynUser = pPvt->pasynUser;
    asynStatus    status;
    asynInterface *pasynInterface;
    dbCommon      *precord = pPvt->precord;

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
            printf("%s %s::initDrvUser drvUserCreate failed %s\n",
                     precord->name, driverName, pasynUser->errorMessage);
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

    len = strlen(pPvt->userParam);
    if(len<=0) {
        printf("%s  no userParam\n",precord->name);
        precord->pact = 1;
        recGblSetSevr(precord,LINK_ALARM,INVALID_ALARM);
        return INIT_ERROR;
    }
    pPvt->buffer = callocMustSucceed(len,sizeof(char),"devAsynOctet");
    dbTranslateEscape(pPvt->buffer,pPvt->userParam);
    pPvt->bufSize = len;
    pPvt->bufLen = strlen(pPvt->buffer);
    return INIT_OK;
}

static int initDbAddr(devPvt *pPvt)
{
    char      *userParam;
    dbCommon *precord = pPvt->precord;

    userParam = pPvt->userParam;
    if(dbNameToAddr(userParam,&pPvt->dbAddr)) {
        printf("%s %s::initDbAddr record %s not present\n",
            precord->name, driverName, userParam);
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

    pPvt->result.status = poctet->write(octetPvt,pasynUser,message,nbytes,&nbytesTransfered);
    pPvt->result.time = pPvt->pasynUser->timestamp;
    pPvt->result.alarmStatus = pPvt->pasynUser->alarmStatus;
    pPvt->result.alarmSeverity = pPvt->pasynUser->alarmSeverity;
    if(pPvt->result.status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s %s::writeIt failed %s\n",
            precord->name, driverName, pasynUser->errorMessage);
        return pPvt->result.status;
    }
    if(nbytes != nbytesTransfered) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s %s::writeIt requested %lu but sent %lu bytes\n",
            precord->name, driverName, (unsigned long)nbytes, (unsigned long)nbytesTransfered);
        recGblSetSevr(precord, WRITE_ALARM, MINOR_ALARM);
        return asynError;
    }
    asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,message,nbytes,
       "%s %s::writeIt\n",precord->name, driverName);
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

    pPvt->result.status = poctet->read(octetPvt,pasynUser,message,maxBytes,
        nBytesRead,&eomReason);
    pPvt->result.time = pPvt->pasynUser->timestamp;
    pPvt->result.alarmStatus = pPvt->pasynUser->alarmStatus;
    pPvt->result.alarmSeverity = pPvt->pasynUser->alarmSeverity;
    if(pPvt->result.status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s %s::readIt failed %s\n",
            precord->name, driverName, pasynUser->errorMessage);
        return pPvt->result.status;
    }
    asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,message,*nBytesRead,
       "%s %s::readIt eomReason %d\n",precord->name, driverName, eomReason);
    return pPvt->result.status;
}

static void reportQueueRequestStatus(devPvt *pPvt, asynStatus status)
{
    if (pPvt->previousQueueRequestStatus != status) {
        pPvt->previousQueueRequestStatus = status;
        if (status == asynSuccess) {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynOctet queueRequest status returned to normal\n", 
                pPvt->precord->name);
        } else {
            asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynOctet queueRequest %s\n", 
                pPvt->precord->name,pPvt->pasynUser->errorMessage);
        }
    }
}
static long processCommon(dbCommon *precord)
{
    devPvt *pPvt = (devPvt *)precord->dpvt;
    waveformRecord *pwf = (waveformRecord *)precord;
    int gotCallbackData;
    
    if (pPvt->ringSize == 0) {
        gotCallbackData = pPvt->gotValue;
    } else {
        gotCallbackData = getRingBufferValue(pPvt);
    }

    if (!gotCallbackData && precord->pact == 0) {
        if(pPvt->canBlock) precord->pact = 1;
        pPvt->result.status = pasynManager->queueRequest(
           pPvt->pasynUser, asynQueuePriorityMedium, 0.0);
        if((pPvt->result.status==asynSuccess) && pPvt->canBlock) return 0;
        if(pPvt->canBlock) precord->pact = 0;
        reportQueueRequestStatus(pPvt, pPvt->result.status);
    }
    if (gotCallbackData) {
        int len;
        if (pPvt->ringSize == 0) {
            /* Data has already been copied to the record in interruptCallback */
            pPvt->gotValue--;
            if (pPvt->isWaveform && (pPvt->result.status == asynSuccess)) pwf->nord = pPvt->nord;
            if (pPvt->gotValue) {
                asynPrint(pPvt->pasynUser, ASYN_TRACE_WARNING,
                    "%s %s::processCommon, "
                    "warning: multiple interrupt callbacks between processing\n",
                     precord->name, driverName);
            }
        } else {
            /* Copy data from ring buffer */
            ringBufferElement *rp = &pPvt->result;
            /* Need to copy the array with the lock because that is shared even though
               pPvt->result is a copy */
            epicsMutexLock(pPvt->ringBufferLock);
            if (rp->status == asynSuccess) {
                memcpy(pPvt->pValue, rp->pValue, rp->len);
                if (pPvt->isWaveform) pwf->nord = rp->len;
            }
            precord->time = rp->time;
            epicsMutexUnlock(pPvt->ringBufferLock);
        }
        len = strlen(pPvt->pValue);
        asynPrintIO(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
            pPvt->pValue, len,
            "%s %s::processCommon len=%d,  data:",
            precord->name, driverName, len);
    }

    pasynEpicsUtils->asynStatusToEpicsAlarm(pPvt->result.status, 
                                            pPvt->isOutput ? WRITE_ALARM : READ_ALARM, &pPvt->result.alarmStatus,
                                            INVALID_ALARM, &pPvt->result.alarmSeverity);
    recGblSetSevr(precord, pPvt->result.alarmStatus, pPvt->result.alarmSeverity);
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

    if(pr->pact) callbackRequestProcessCallback(&pPvt->callback,pr->prio,pr);
}

static long initSiCmdResponse(stringinRecord *psi)
{
    devPvt     *pPvt;
    int        status;

    status = initCommon((dbCommon *)psi, &psi->inp, callbackSiCmdResponse, 
                        0, 0, 0, psi->val, sizeof(psi->val));
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
                        0, 0, 0, psi->val, sizeof(psi->val));
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
                      0, 0, 1, psi->val, sizeof(psi->val));
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
                      1, 0, 1, pso->val, sizeof(pso->val));
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
                        0, 1, 0, pwf->bptr, pwf->nelm);
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
            pwf->nord = (epicsUInt32)nBytesRead;
            if (nBytesRead < pwf->nelm) pbuf[nBytesRead] = 0;
        }
    }
    finish((dbCommon *)pwf);
}

static long initWfWriteRead(waveformRecord *pwf)
{
    int        status;

    status = initCommon((dbCommon *)pwf, &pwf->inp, callbackWfWriteRead,
                        0, 1, 0, pwf->bptr, pwf->nelm);
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

    dbStatus = dbGet(&pPvt->dbAddr, DBR_STRING, raw, 0, 0, 0);
    raw[MAX_STRING_SIZE] = 0;
    if(dbStatus) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s dbGet failed\n",pwf->name);
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
            pwf->nord = (epicsUInt32)nBytesRead;
            if (nBytesRead < pwf->nelm) pbuf[nBytesRead] = 0;
        }
    }
    finish((dbCommon *)pwf);
}

static long initWfRead(waveformRecord *pwf)
{
    return initCommon((dbCommon *)pwf, &pwf->inp, callbackWfRead,
                      0, 1, 1, pwf->bptr, pwf->nelm);
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
        pwf->nord = (epicsUInt32)nBytesRead;
        if (nBytesRead < pwf->nelm) pbuf[nBytesRead] = 0;
    }
    finish((dbCommon *)pwf);
}

static long initWfWrite(waveformRecord *pwf)
{
    return initCommon((dbCommon *)pwf, &pwf->inp, callbackWfWrite,
                      1, 1, 1, pwf->bptr, pwf->nelm);
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
                      1, 1, 1, pwf->bptr, pwf->nelm);
}

static void callbackWfWriteBinary(asynUser *pasynUser)
{
    devPvt          *pPvt = (devPvt *)pasynUser->userPvt;
    waveformRecord  *pwf = (waveformRecord *)pPvt->precord;

    writeIt(pasynUser, pwf->bptr, pwf->nord);
    finish((dbCommon *)pwf);
}
