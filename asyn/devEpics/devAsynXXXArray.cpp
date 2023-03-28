/* devAsynXXXArray.cpp */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
    Authors:  Mark Rivers
    30-Sept-2022
*/

#include <alarm.h>
#include <callback.h>
#include <devSup.h>
#include <dbCommon.h>
#include <dbAccess.h>
#include <dbStaticLib.h>
#include <epicsMutex.h>
#include <epicsString.h>
#include <errlog.h>
#include <menuFtype.h>
#include <recGbl.h>
#include <dbScan.h>
#include <waveformRecord.h>
#include <aaoRecord.h>
#include <aaiRecord.h>

#include <epicsExport.h>
#include <asynDriver.h>
#include <asynDrvUser.h>
#include <asynEpicsUtils.h>
#include <asynInt8Array.h>
#include <asynInt16Array.h>
#include <asynInt32Array.h>
#include <asynInt64Array.h>
#include <asynFloat32Array.h>
#include <asynFloat64Array.h>
#include "devEpicsPvt.h"

#define DEFAULT_RING_BUFFER_SIZE 0
#define INIT_OK 0
#define INIT_DO_NOT_CONVERT 2
#define INIT_ERROR -1

static const char *driverName = "devAsynXXXArray";


// We use an anonymous namespace to hide these definitions
namespace {

template <typename RECORD_TYPE, typename INTERFACE, typename INTERRUPT, typename EPICS_TYPE>
class devAsynXXXArray
{

private:
    struct ringBufferElement {
        EPICS_TYPE          *pValue;
        size_t              len;
        epicsTimeStamp      time;
        asynStatus          status;
        epicsAlarmCondition alarmStatus;
        epicsAlarmSeverity  alarmSeverity;
    };

    RECORD_TYPE         *pRecord_;
    asynUser            *pasynUser_;
    INTERFACE           *pInterface_;
    void                *pInterfacePvt_;
    void                *registrarPvt_;
    int                 canBlock_;
    CALLBACK            callback_;
    IOSCANPVT           ioScanPvt_;
    asynStatus          lastStatus_;
    bool                isOutput_;
    epicsMutexId        ringBufferLock_;
    ringBufferElement   *ringBuffer_;
    int                 ringHead_;
    int                 ringTail_;
    int                 ringSize_;
    int                 ringBufferOverflows_;
    ringBufferElement   result_;
    int                 gotValue_; /* For interruptCallbackInput */
    INTERRUPT           interruptCallback_;
    char                *portName_;
    char                *userParam_;
    int                 addr_;
    const char          *interfaceType_;
    int                 signedType_;
    int                 unsignedType_;
    asynStatus          previousQueueRequestStatus_;

public:

    devAsynXXXArray(dbCommon *pRecord, DBLINK *plink, int signedType, int unsignedType, bool isOutput, const char *interfaceType,
                    userCallback qrCallback, INTERRUPT interruptCallback):
        pRecord_((RECORD_TYPE*) pRecord),
        lastStatus_(asynSuccess),
        isOutput_(isOutput),
        ringBuffer_(0),
        ringHead_(0),
        ringTail_(0),
        ringSize_(0),
        ringBufferOverflows_(0),
        gotValue_(0),
        interruptCallback_(interruptCallback),
        interfaceType_(epicsStrDup(interfaceType)),
        signedType_(signedType),
        unsignedType_(unsignedType),
        previousQueueRequestStatus_(asynSuccess)
    {
        int status;
        asynInterface *pasynInterface;

        static const char *functionName = "devAsynXXXArray";

        pRecord_->dpvt = this;
        pasynUser_ = pasynManager->createAsynUser(qrCallback, 0);
        pasynUser_->userPvt = this;
        ringBufferLock_ = epicsMutexCreate();
        /* This device support only supports signed and unsigned versions of the EPICS data type  */
        if ((pRecord_->ftvl != this->signedType_) && (pRecord_->ftvl != this->unsignedType_)) {
            errlogPrintf("%s::%s, %s field type must be SIGNED_TYPE or UNSIGNED_TYPE\n",
                         driverName, functionName, pRecord_->name);
            goto bad;
        }
        /* Parse the link to get addr and port */
        status = pasynEpicsUtils->parseLink(pasynUser_, plink,
                    &portName_, &addr_, &userParam_);
        if (status != asynSuccess) {
            errlogPrintf("%s::%s, %s error in link %s\n",
                         driverName, functionName, pRecord_->name, pasynUser_->errorMessage);
            goto bad;
        }
        status = pasynManager->connectDevice(pasynUser_, portName_, addr_);
        if (status != asynSuccess) {
            errlogPrintf("%s::%s, %s connectDevice failed %s\n",
                         driverName, functionName, pRecord_->name, pasynUser_->errorMessage);
            goto bad;
        }
        pasynInterface = pasynManager->findInterface(pasynUser_, asynDrvUserType, 1);
        if (pasynInterface && userParam_) {
            asynDrvUser *pasynDrvUser;
            void       *drvPvt;

            pasynDrvUser = (asynDrvUser *)pasynInterface->pinterface;
            drvPvt = pasynInterface->drvPvt;
            status = pasynDrvUser->create(drvPvt, pasynUser_, userParam_, 0,0);
            if (status != asynSuccess) {
                errlogPrintf(
                    "%s::%s, %s drvUserCreate failed %s\n",
                    driverName, functionName, pRecord_->name, pasynUser_->errorMessage);
                goto bad;
            }
        }
        pasynInterface = pasynManager->findInterface(pasynUser_, interfaceType_, 1);
        if (!pasynInterface) {
            errlogPrintf(
                "%s::%s, %s find %s interface failed %s\n",
                 driverName, functionName, pRecord_->name, this->interfaceType_, pasynUser_->errorMessage);
            goto bad;
        }
        pInterface_ = (INTERFACE*) pasynInterface->pinterface;
        pInterfacePvt_ = pasynInterface->drvPvt;
        /* If this is an output record and the info field "asyn:READBACK" is 1
         * then register for callbacks on output records */
        if (isOutput_) {
            int enableCallbacks=0;
            const char *callbackString = asynDbGetInfo((dbCommon*)pRecord_, "asyn:READBACK");
            if (callbackString) enableCallbacks = atoi(callbackString);
            if (enableCallbacks) {
                status = createRingBuffer();
                if (status != asynSuccess) goto bad;
                status = pInterface_->registerInterruptUser(
                   pInterfacePvt_, pasynUser_,
                   interruptCallback_, this, &registrarPvt_);
                if (status != asynSuccess) {
                    printf("%s %s::%s error calling registerInterruptUser %s\n",
                           pRecord_->name, driverName, functionName, pasynUser_->errorMessage);
                }
            }
        }
        scanIoInit(&ioScanPvt_);
        /* Determine if device can block */
        pasynManager->canBlock(pasynUser_, &canBlock_);
        return;
    bad:
        recGblSetSevr(pRecord_, LINK_ALARM, INVALID_ALARM);
        pRecord_->pact=1;
    }

    long createRingBuffer()
    {
        int status;
        const char *sizeString;
        static const char *functionName = "createRingBuffer";

        if (!ringBuffer_) {
            DBENTRY *pdbentry = dbAllocEntry(pdbbase);
            ringSize_ = DEFAULT_RING_BUFFER_SIZE;
            status = dbFindRecord(pdbentry, pRecord_->name);
            if (status)
                asynPrint(pasynUser_, ASYN_TRACE_ERROR,
                    "%s %s::%s error finding record status=%d\n",
                    pRecord_->name, driverName, functionName, status);
            sizeString = dbGetInfo(pdbentry, "asyn:FIFO");
            if (sizeString) ringSize_ = atoi(sizeString);
            if (ringSize_ > 0) {
                int i;
                ringBuffer_ = (ringBufferElement *) callocMustSucceed(
                                  ringSize_, sizeof(*ringBuffer_),
                                  "devAsynXXXArray::createRingBuffer creating ring buffer");
                /* Allocate array for each ring buffer element */
                for (i=0; i<ringSize_; i++) {
                    ringBuffer_[i].pValue =
                        (EPICS_TYPE *)callocMustSucceed(
                            pRecord_->nelm, sizeof(EPICS_TYPE),
                            "devAsynXXXArray::createRingBuffer creating ring element array");
                }
            }
        }
        return asynSuccess;
    }

    long getIoIntInfo(int cmd, IOSCANPVT *iopvt)
    {
        int status;
        static const char *functionName = "getIoIntInfo";

        /* If initCommon failed then pInterface is NULL, return error */
        if (!pInterface_) return -1;

        if (cmd == 0) {
            /* Add to scan list.  Register interrupts */
            asynPrint(pasynUser_, ASYN_TRACE_FLOW,
                "%s %s::%s registering interrupt\n",
                pRecord_->name, driverName, functionName);
            createRingBuffer();
            status = pInterface_->registerInterruptUser(
               pInterfacePvt_, pasynUser_,
               interruptCallback_, this, &registrarPvt_);
            if (status != asynSuccess) {
                asynPrint(pasynUser_, ASYN_TRACE_ERROR,
                          "%s %s::%s error calling registerInterruptUser %s\n",
                          pRecord_->name, driverName, functionName, pasynUser_->errorMessage);
            }
        } else {
            asynPrint(pasynUser_, ASYN_TRACE_FLOW,
                "%s %s::%s cancelling interrupt\n",
                 pRecord_->name, driverName, functionName);
            status = pInterface_->cancelInterruptUser(pInterfacePvt_,
                 pasynUser_, registrarPvt_);
            if (status != asynSuccess) {
                asynPrint(pasynUser_, ASYN_TRACE_ERROR,
                          "%s %s::%s error calling cancelInterruptUser %s\n",
                          pRecord_->name, driverName, functionName, pasynUser_->errorMessage);
            }
        }
        *iopvt = ioScanPvt_;
        return INIT_OK;
    }

    void reportQueueRequestStatus(asynStatus status)
    {
        if (previousQueueRequestStatus_ != status) {
            previousQueueRequestStatus_ = status;
            if (status == asynSuccess) {
                asynPrint(pasynUser_, ASYN_TRACE_ERROR,
                    "%s %s queueRequest status returned to normal\n",
                    pRecord_->name, driverName);
            } else {
                asynPrint(pasynUser_, ASYN_TRACE_ERROR,
                    "%s %s queueRequest %s\n",
                    pRecord_->name, driverName, pasynUser_->errorMessage);
            }
        }
    }

    long process()
    {
        int newInputData;
        asynStatus status;
        static const char *functionName = "process";

        if (ringSize_ == 0) {
            newInputData = gotValue_;
        } else {
            newInputData = getRingBufferValue();
        }
        if (!newInputData && !pRecord_->pact) {   /* This is an initial call from record */
            if(canBlock_) pRecord_->pact = 1;
            status = pasynManager->queueRequest(pasynUser_, asynQueuePriorityLow, 0);
            if ((status == asynSuccess) && canBlock_) return 0;
            if (canBlock_) pRecord_->pact = 0;
            reportQueueRequestStatus(status);
        }
        if (newInputData) {
            if (ringSize_ == 0){
                /* Data has already been copied to the record in interruptCallback */
                gotValue_--;
                if (gotValue_) {
                    asynPrint(pasynUser_, ASYN_TRACE_WARNING,
                        "%s %s::%s, "
                        "warning: multiple interrupt callbacks between processing\n",
                         pRecord_->name, driverName, functionName);
                }
            } else {
                /* Copy data from ring buffer */
                EPICS_TYPE *pData = (EPICS_TYPE *)pRecord_->bptr;
                ringBufferElement *rp = &result_;
                int i;
                /* Need to copy the array with the lock because that is shared even though
                   result_ is a copy */
                if (rp->status == asynSuccess) {
                    epicsMutexLock(ringBufferLock_);
                    for (i=0; i<(int)rp->len; i++) pData[i] = rp->pValue[i];
                    epicsMutexUnlock(ringBufferLock_);
                    pRecord_->nord = (epicsUInt32)rp->len;
                    asynPrintIO(pasynUser_, ASYN_TRACEIO_DEVICE,
                        (char *)pRecord_->bptr, pRecord_->nord*sizeof(EPICS_TYPE),
                        "%s %s::%s nord=%d, pRecord_->bptr data:",
                        pRecord_->name, driverName, driverName, pRecord_->nord);
                }
                pRecord_->time = rp->time;
            }
        }
        pasynEpicsUtils->asynStatusToEpicsAlarm(result_.status,
                                                READ_ALARM, &result_.alarmStatus,
                                                INVALID_ALARM, &result_.alarmSeverity);
        recGblSetSevr(pRecord_, result_.alarmStatus, result_.alarmSeverity);
        if (result_.status == asynSuccess) {
            pRecord_->udf = 0;
            return 0;
        } else {
            result_.status = asynSuccess;
            return -1;
        }
    }

    void queueRequestCallback()
    {
        static const char *functionName = "queueRequestCallback";
        size_t nread;

        if (isOutput_) {
            result_.status = pInterface_->write(pInterfacePvt_, pasynUser_,
                                                (EPICS_TYPE *) pRecord_->bptr, pRecord_->nord);
        } else {
            result_.status = pInterface_->read(pInterfacePvt_, pasynUser_, (EPICS_TYPE *) pRecord_->bptr,
                                               pRecord_->nelm, &nread);
        }
        result_.time = pasynUser_->timestamp;
        result_.alarmStatus = (epicsAlarmCondition) pasynUser_->alarmStatus;
        result_.alarmSeverity = (epicsAlarmSeverity) pasynUser_->alarmSeverity;
        if (result_.status == asynSuccess) {
            if (!isOutput_) {
                pRecord_->udf=0;
                pRecord_->nord = (epicsUInt32)nread;
            }
            asynPrint(pasynUser_, ASYN_TRACEIO_DEVICE,
                "%s %s::%s OK\n", pRecord_->name, driverName, functionName);
        } else {
            if (result_.status != lastStatus_) {
                asynPrint(pasynUser_, ASYN_TRACE_ERROR,
                    "%s %s::%s %s error %s\n",
                    pRecord_->name, driverName, functionName, isOutput_ ? "write" : "read", pasynUser_->errorMessage);
            }
        }
        lastStatus_ = result_.status;
        if (pRecord_->pact) callbackRequestProcessCallback(&callback_, pRecord_->prio, pRecord_);
    }

    int getRingBufferValue()
    {
        int ret = 0;
        static const char *functionName = "getRingBufferValue ";

        epicsMutexLock(ringBufferLock_);
        if (ringTail_ != ringHead_) {
            if (ringBufferOverflows_ > 0) {
                asynPrint(pasynUser_, ASYN_TRACE_WARNING,
                    "%s %s::%s error, %d ring buffer overflows\n",
                    pRecord_->name, driverName, functionName, ringBufferOverflows_);
                ringBufferOverflows_ = 0;
            }
            result_ = ringBuffer_[ringTail_];
            ringTail_ = (ringTail_ == ringSize_-1) ? 0 : ringTail_ + 1;
            ret = 1;
        }
        epicsMutexUnlock(ringBufferLock_);
        return ret;
    }

    void interruptCallback(asynUser *pasynUser, EPICS_TYPE *value, size_t len)
    {
        int i;
        EPICS_TYPE *pData = (EPICS_TYPE *)pRecord_->bptr;
        static const char *functionName = "interruptCallback";

        asynPrintIO(pasynUser_, ASYN_TRACEIO_DEVICE,
            (char *)value, len*sizeof(EPICS_TYPE),
            "%s %s::%s ringSize=%d, len=%d, callback data:",
            pRecord_->name, driverName, functionName, ringSize_, (int)len);
        if (ringSize_ == 0) {
            /* Not using a ring buffer */
            dbScanLock((dbCommon *)pRecord_);
            if (len > pRecord_->nelm) len = pRecord_->nelm;
            if (pasynUser->auxStatus == asynSuccess) {
                for (i=0; i<(int)len; i++) pData[i] = value[i];
                pRecord_->nord = (epicsUInt32)len;
            }
            pRecord_->time = pasynUser->timestamp;
            result_.status = (asynStatus) pasynUser->auxStatus;
            result_.alarmStatus = (epicsAlarmCondition) pasynUser->alarmStatus;
            result_.alarmSeverity = (epicsAlarmSeverity) pasynUser->alarmSeverity;
            gotValue_++;
            dbScanUnlock((dbCommon *)pRecord_);
            if (isOutput_)
                scanOnce((dbCommon *)pRecord_);
            else
                scanIoRequest(ioScanPvt_);
        } else {
            /* Using a ring buffer */
            ringBufferElement *rp;

            /* If interruptAccept is false we just return.  This prevents more ring pushes than pops.
             * There will then be nothing in the ring buffer, so the first
             * read will do a read from the driver, which should be OK. */
            if (!interruptAccept) return;

            epicsMutexLock(ringBufferLock_);
            rp = &ringBuffer_[ringHead_];
            if (len > pRecord_->nelm) len = pRecord_->nelm;
            rp->len = len;
            for (i=0; i<(int)len; i++) rp->pValue[i] = value[i];
            rp->time = pasynUser->timestamp;
            rp->status = (asynStatus) pasynUser->auxStatus;
            rp->alarmStatus = (epicsAlarmCondition) pasynUser->alarmStatus;
            rp->alarmSeverity = (epicsAlarmSeverity) pasynUser->alarmSeverity;
            ringHead_ = (ringHead_ == ringSize_ - 1) ? 0 : ringHead_ + 1;
            if (ringHead_ == ringTail_) {
                /* There was no room in the ring buffer.  In the past we just threw away
                 * the new value.  However, it is better to remove the oldest value from the
                 * ring buffer and add the new one.  That way the final value the record receives
                 * is guaranteed to be the most recent value */
                ringTail_ = (ringTail_ == ringSize_ - 1) ? 0 : ringTail_ + 1;
                ringBufferOverflows_++;
            } else {
                /* We only need to request the record to process if we added a new
                 * element to the ring buffer, not if we just replaced an element. */
                if (isOutput_)
                    scanOnce((dbCommon *)pRecord_);
                else
                    scanIoRequest(ioScanPvt_);
            }
            epicsMutexUnlock(ringBufferLock_);
        }
    }
};

struct analogDset { /* analog  dset */
    long          number;
    DEVSUPFUN     dev_report;
    DEVSUPFUN     init;
    long          (*init_record)(dbCommon *pr);
    long          (*get_ioint_info)(int cmd, dbCommon *pr, IOSCANPVT *iopvt);
    long          (*process)(dbCommon *pr);
    DEVSUPFUN     special_linconv;
};

} // End of namespace

#define MAKE_DEVSUP(DSET, REC, LINK, INTERFACE, INTERFACE_NAME, INTERRUPT, EPICS_TYPE,     \
                    INIT_FUNC, GET_INFO_FUNC, PROC_FUNC, QRCB_FUNC, INTCB_FUNC,            \
                    SIGNED_TYPE, UNSIGNED_TYPE, IS_OUTPUT)                                 \
static long GET_INFO_FUNC(int cmd, dbCommon *pr, IOSCANPVT *iopvt) {                       \
    devAsynXXXArray<REC, INTERFACE, INTERRUPT, EPICS_TYPE> *pObj =                         \
        (devAsynXXXArray<REC, INTERFACE, INTERRUPT, EPICS_TYPE> *) pr->dpvt;               \
    return pObj->getIoIntInfo(cmd, iopvt);                                                 \
}                                                                                          \
static long PROC_FUNC(dbCommon *pr) {                                                      \
    devAsynXXXArray<REC, INTERFACE, INTERRUPT, EPICS_TYPE> *pObj =                         \
        (devAsynXXXArray<REC, INTERFACE, INTERRUPT, EPICS_TYPE> *) pr->dpvt;               \
    return pObj->process();                                                                \
}                                                                                          \
static void QRCB_FUNC(asynUser *pasynUser) {                                               \
    devAsynXXXArray<REC, INTERFACE, INTERRUPT, EPICS_TYPE> *pObj =                         \
        (devAsynXXXArray<REC, INTERFACE, INTERRUPT, EPICS_TYPE> *) pasynUser->userPvt;     \
    pObj->queueRequestCallback();                                                          \
}                                                                                          \
static void INTCB_FUNC(void *drvPvt, asynUser *pasynUser, EPICS_TYPE *value, size_t len) { \
    devAsynXXXArray<REC, INTERFACE, INTERRUPT, EPICS_TYPE> *pObj =                         \
        (devAsynXXXArray<REC, INTERFACE, INTERRUPT, EPICS_TYPE> *) drvPvt;                 \
    pObj->interruptCallback(pasynUser, value, len);                                        \
}                                                                                          \
static long INIT_FUNC(dbCommon *pr) {                                                      \
    new devAsynXXXArray<REC, INTERFACE, INTERRUPT, EPICS_TYPE>                             \
                        (pr, &((REC *)pr)->LINK, SIGNED_TYPE, UNSIGNED_TYPE, IS_OUTPUT,    \
                        INTERFACE_NAME, QRCB_FUNC, INTCB_FUNC);                            \
    return 0;                                                                              \
}                                                                                          \
static analogDset DSET = {6, 0, 0, INIT_FUNC, GET_INFO_FUNC, PROC_FUNC, 0};                       \
epicsExportAddress(dset, DSET);

extern "C" {
// 8-bit integer arrays
MAKE_DEVSUP(asynInt8ArrayWfIn, waveformRecord, inp, asynInt8Array, "asynInt8Array", interruptCallbackInt8Array, epicsInt8,
            initInt8WfIn, getInfoInt8WfIn, processInt8WfIn, qrCallbackInt8WfIn, intCallbackInt8WfIn,
            menuFtypeCHAR, menuFtypeUCHAR, false);
MAKE_DEVSUP(asynInt8ArrayWfOut, waveformRecord, inp, asynInt8Array, "asynInt8Array", interruptCallbackInt8Array, epicsInt8,
            initInt8WfOut, getInfoInt8WfOut, processInt8WfOut, qrCallbackInt8WfOut, intCallbackInt8WfOut,
            menuFtypeCHAR, menuFtypeUCHAR, true);
MAKE_DEVSUP(asynInt8ArrayAai, aaiRecord, inp, asynInt8Array, "asynInt8Array", interruptCallbackInt8Array, epicsInt8,
            initInt8AaiIn, getInfoInt8Aai, processInt8Aai, qrCallbackInt8Aai, intCallbackInt8Aai,
            menuFtypeCHAR, menuFtypeUCHAR, false);
MAKE_DEVSUP(asynInt8ArrayAao, aaoRecord, out, asynInt8Array, "asynInt8Array", interruptCallbackInt8Array, epicsInt8,
            initInt8Aao, getInfoInt8Aao, processInt8Aao, qrCallbackInt8Aao, intCallbackInt8Aao,
            menuFtypeCHAR, menuFtypeUCHAR, true);

// 16-bit integer arrays
MAKE_DEVSUP(asynInt16ArrayWfIn, waveformRecord, inp, asynInt16Array, "asynInt16Array", interruptCallbackInt16Array, epicsInt16,
            initInt16WfIn, getInfoInt16WfIn, processInt16WfIn, qrCallbackInt16WfIn, intCallbackInt16WfIn,
            menuFtypeSHORT, menuFtypeUSHORT, false);
MAKE_DEVSUP(asynInt16ArrayWfOut, waveformRecord, inp, asynInt16Array, "asynInt16Array", interruptCallbackInt16Array, epicsInt16,
            initInt16WfOut, getInfoInt16WfOut, processInt16WfOut, qrCallbackInt16WfOut, intCallbackInt16WfOut,
            menuFtypeSHORT, menuFtypeUSHORT, true);
MAKE_DEVSUP(asynInt16ArrayAai, aaiRecord, inp, asynInt16Array, "asynInt16Array", interruptCallbackInt16Array, epicsInt16,
            initInt16AaiIn, getInfoInt16Aai, processInt16Aai, qrCallbackInt16Aai, intCallbackInt16Aai,
            menuFtypeSHORT, menuFtypeUSHORT, false);
MAKE_DEVSUP(asynInt16ArrayAao, aaoRecord, out, asynInt16Array, "asynInt16Array", interruptCallbackInt16Array, epicsInt16,
            initInt16Aao, getInfoInt16Aao, processInt16Aao, qrCallbackInt16Aao, intCallbackInt16Aao,
            menuFtypeSHORT, menuFtypeUSHORT, true);

// 32-bit integer arrays
MAKE_DEVSUP(asynInt32ArrayWfIn, waveformRecord, inp, asynInt32Array, "asynInt32Array", interruptCallbackInt32Array, epicsInt32,
            initInt32WfIn, getInfoInt32WfIn, processInt32WfIn, qrCallbackInt32WfIn, intCallbackInt32WfIn,
            menuFtypeLONG, menuFtypeULONG, false);
MAKE_DEVSUP(asynInt32ArrayWfOut, waveformRecord, inp, asynInt32Array, "asynInt32Array", interruptCallbackInt32Array, epicsInt32,
            initInt32WfOut, getInfoInt32WfOut, processInt32WfOut, qrCallbackInt32WfOut, intCallbackInt32WfOut,
            menuFtypeLONG, menuFtypeULONG, true);
MAKE_DEVSUP(asynInt32ArrayAai, aaiRecord, inp, asynInt32Array, "asynInt32Array", interruptCallbackInt32Array, epicsInt32,
            initInt32AaiIn, getInfoInt32Aai, processInt32Aai, qrCallbackInt32Aai, intCallbackInt32Aai,
            menuFtypeLONG, menuFtypeULONG, false);
MAKE_DEVSUP(asynInt32ArrayAao, aaoRecord, out, asynInt32Array, "asynInt32Array", interruptCallbackInt32Array, epicsInt32,
            initInt32Aao, getInfoInt32Aao, processInt32Aao, qrCallbackInt32Aao, intCallbackInt32Aao,
            menuFtypeLONG, menuFtypeULONG, true);

// 64-bit integer arrays
#ifdef HAVE_DEVINT64
MAKE_DEVSUP(asynInt64ArrayWfIn, waveformRecord, inp, asynInt64Array, "asynInt64Array", interruptCallbackInt64Array, epicsInt64,
            initInt64WfIn, getInfoInt64WfIn, processInt64WfIn, qrCallbackInt64WfIn, intCallbackInt64WfIn,
            menuFtypeINT64, menuFtypeUINT64, false);
MAKE_DEVSUP(asynInt64ArrayWfOut, waveformRecord, inp, asynInt64Array, "asynInt64Array", interruptCallbackInt64Array, epicsInt64,
            initInt64WfOut, getInfoInt64WfOut, processInt64WfOut, qrCallbackInt64WfOut, intCallbackInt64WfOut,
            menuFtypeINT64, menuFtypeUINT64, true);
MAKE_DEVSUP(asynInt64ArrayAai, aaiRecord, inp, asynInt64Array, "asynInt64Array", interruptCallbackInt64Array, epicsInt64,
            initInt64AaiIn, getInfoInt64Aai, processInt64Aai, qrCallbackInt64Aai, intCallbackInt64Aai,
            menuFtypeINT64, menuFtypeUINT64, false);
MAKE_DEVSUP(asynInt64ArrayAao, aaoRecord, out, asynInt64Array, "asynInt64Array", interruptCallbackInt64Array, epicsInt64,
            initInt64Aao, getInfoInt64Aao, processInt64Aao, qrCallbackInt64Aao, intCallbackInt64Aao,
            menuFtypeINT64, menuFtypeUINT64, true);
#endif

// 32-bit float arrays
MAKE_DEVSUP(asynFloat32ArrayWfIn, waveformRecord, inp, asynFloat32Array, "asynFloat32Array", interruptCallbackFloat32Array, epicsFloat32,
            initFloat32WfIn, getInfoFloat32WfIn, processFloat32WfIn, qrCallbackFloat32WfIn, intCallbackFloat32WfIn,
            menuFtypeFLOAT, menuFtypeFLOAT, false);
MAKE_DEVSUP(asynFloat32ArrayWfOut, waveformRecord, inp, asynFloat32Array, "asynFloat32Array", interruptCallbackFloat32Array, epicsFloat32,
            initFloat32WfOut, getInfoFloat32WfOut, processFloat32WfOut, qrCallbackFloat32WfOut, intCallbackFloat32WfOut,
            menuFtypeFLOAT, menuFtypeFLOAT, true);
MAKE_DEVSUP(asynFloat32ArrayAai, aaiRecord, inp, asynFloat32Array, "asynFloat32Array", interruptCallbackFloat32Array, epicsFloat32,
            initFloat32AaiIn, getInfoFloat32Aai, processFloat32Aai, qrCallbackFloat32Aai, intCallbackFloat32Aai,
            menuFtypeFLOAT, menuFtypeFLOAT, false);
MAKE_DEVSUP(asynFloat32ArrayAao, aaoRecord, out, asynFloat32Array, "asynFloat32Array", interruptCallbackFloat32Array, epicsFloat32,
            initFloat32Aao, getInfoFloat32Aao, processFloat32Aao, qrCallbackFloat32Aao, intCallbackFloat32Aao,
            menuFtypeFLOAT, menuFtypeFLOAT, true);

// 64-bit float arrays
MAKE_DEVSUP(asynFloat64ArrayWfIn, waveformRecord, inp, asynFloat64Array, "asynFloat64Array", interruptCallbackFloat64Array, epicsFloat64,
            initFloat64WfIn, getInfoFloat64WfIn, processFloat64WfIn, qrCallbackFloat64WfIn, intCallbackFloat64WfIn,
            menuFtypeDOUBLE, menuFtypeDOUBLE, false);
MAKE_DEVSUP(asynFloat64ArrayWfOut, waveformRecord, inp, asynFloat64Array, "asynFloat64Array", interruptCallbackFloat64Array, epicsFloat64,
            initFloat64WfOut, getInfoFloat64WfOut, processFloat64WfOut, qrCallbackFloat64WfOut, intCallbackFloat64WfOut,
            menuFtypeDOUBLE, menuFtypeDOUBLE, true);
MAKE_DEVSUP(asynFloat64ArrayAai, aaiRecord, inp, asynFloat64Array, "asynFloat64Array", interruptCallbackFloat64Array, epicsFloat64,
            initFloat64AaiIn, getInfoFloat64Aai, processFloat64Aai, qrCallbackFloat64Aai, intCallbackFloat64Aai,
            menuFtypeDOUBLE, menuFtypeDOUBLE, false);
MAKE_DEVSUP(asynFloat64ArrayAao, aaoRecord, out, asynFloat64Array, "asynFloat64Array", interruptCallbackFloat64Array, epicsFloat64,
            initFloat64Aao, getInfoFloat64Aao, processFloat64Aao, qrCallbackFloat64Aao, intCallbackFloat64Aao,
            menuFtypeDOUBLE, menuFtypeDOUBLE, true);

} /* extern "C" */
