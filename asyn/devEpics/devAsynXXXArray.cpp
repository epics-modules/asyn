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
#include <asynInt32Array.h>

#define DEFAULT_RING_BUFFER_SIZE 0
#define INIT_OK 0
#define INIT_DO_NOT_CONVERT 2
#define INIT_ERROR -1

static const char *driverName = "devAsynXXXArray";

template <typename RECORD_TYPE, typename INTERFACE, typename INTERRUPT, typename EPICS_TYPE>
class devAsynXXXArray
{
public:

    devAsynXXXArray(RECORD_TYPE *pRecord, DBLINK *plink, int signedType, int unsignedType, bool isOutput, const char *interfaceType,
                    userCallback qrCallback, INTERRUPT interruptCallback):
        pRecord_(pRecord),
        interruptCallback_(interruptCallback),
        interfaceType_(epicsStrDup(interfaceType)),
        signedType_(signedType),
        unsignedType_(unsignedType)
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
            const char *callbackString;
            DBENTRY *pdbentry = dbAllocEntry(pdbbase);
            status = dbFindRecord(pdbentry, pRecord_->name);
            if (status) {
                asynPrint(pasynUser_, ASYN_TRACE_ERROR,
                    "%s %s::%s error finding record\n",
                    pRecord_->name, driverName, functionName);
                goto bad;
            }
            callbackString = dbGetInfo(pdbentry, "asyn:READBACK");
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
                          "%s %s::%s registerInterruptUser %s\n",                           
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
                          "%s %s::%s cancelInterruptUser %s\n",                             
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
        if (isOutput_)
            queueRequestCallbackOut();
        else
            queueRequestCallbackIn();
    }                                                   

    void queueRequestCallbackOut()                                                    
    {
        static const char *functionName = "processCallbackOut";
                                                                                                                                                                                                       
        asynPrint(pasynUser_, ASYN_TRACEIO_DEVICE,                                                     
                  "%s %s::%s\n", pRecord_->name, driverName, functionName);                                   
        result_.status = pInterface_->write(pInterfacePvt_, pasynUser_,                    
                                            (EPICS_TYPE *) pRecord_->bptr, pRecord_->nord);                              
        result_.time = pasynUser_->timestamp;                                               
        result_.alarmStatus = (epicsAlarmCondition) pasynUser_->alarmStatus;                                      
        result_.alarmSeverity = (epicsAlarmSeverity) pasynUser_->alarmSeverity;                                  
        if (result_.status == asynSuccess) {                                                     
            asynPrint(pasynUser_, ASYN_TRACEIO_DEVICE,                                                 
                "%s %s::%s OK\n", pRecord_->name, driverName, functionName);                                  
        } else {                                                                                      
            if (result_.status != lastStatus_) {                                            
                asynPrint(pasynUser_, ASYN_TRACE_ERROR,                                                
                    "%s %s::%s write error %s\n",                                          
                    pRecord_->name, driverName, functionName, pasynUser_->errorMessage);                                  
            }                                                                                         
        }                                                                                             
        lastStatus_ = result_.status;                                                       
        if (pRecord_->pact) callbackRequestProcessCallback(&callback_, pRecord_->prio, pRecord_);                  
    }                                                                                                 
                                                                                                      
    void queueRequestCallbackIn()                                                     
    {                                                                                                 
        size_t nread;                                                                                 
                                                                                                      
        result_.status = pInterface_->read(pInterfacePvt_, pasynUser_, (EPICS_TYPE *) pRecord_->bptr,          
                                           pRecord_->nelm, &nread);                                  
        asynPrint(pasynUser_, ASYN_TRACEIO_DEVICE,                                                     
                  "%s %s::callbackWfIn\n", pRecord_->name, driverName);                                    
        result_.time = pasynUser_->timestamp;                                               
        result_.alarmStatus = (epicsAlarmCondition) pasynUser_->alarmStatus;                                      
        result_.alarmSeverity = (epicsAlarmSeverity) pasynUser_->alarmSeverity;                                  
        if (result_.status == asynSuccess) {                                                     
            pRecord_->udf=0;                                                                               
            pRecord_->nord = (epicsUInt32)nread;                                                           
        } else {                                                                                      
            if (result_.status != lastStatus_) {                                            
                asynPrint(pasynUser_, ASYN_TRACE_ERROR,                                                
                    "%s %s::callbackWfIn read error %s\n",                                            
                    pRecord_->name, driverName, pasynUser_->errorMessage);                                  
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

};

extern "C" {
static long getInfoWfInt32Out(int cmd, dbCommon *pr, IOSCANPVT *iopvt) {
    devAsynXXXArray<waveformRecord, asynInt32Array, interruptCallbackInt32Array, epicsInt32> *pObj =
        (devAsynXXXArray<waveformRecord, asynInt32Array, interruptCallbackInt32Array, epicsInt32> *) pr->dpvt;
    return pObj->getIoIntInfo(cmd, iopvt);
}
static long processWfInt32Out(dbCommon *pr) {
    devAsynXXXArray<waveformRecord, asynInt32Array, interruptCallbackInt32Array, epicsInt32> *pObj =
        (devAsynXXXArray<waveformRecord, asynInt32Array, interruptCallbackInt32Array, epicsInt32> *) pr->dpvt;
    return pObj->process();
}
static void qrCallbackWfInt32Out(asynUser *pasynUser) {
    devAsynXXXArray<waveformRecord, asynInt32Array, interruptCallbackInt32Array, epicsInt32> *pObj =
        (devAsynXXXArray<waveformRecord, asynInt32Array, interruptCallbackInt32Array, epicsInt32> *) pasynUser->userPvt;
    pObj->queueRequestCallback();
}
static void intCallbackWfInt32Out(void *drvPvt, asynUser *pasynUser, epicsInt32 *value, size_t len) {
    devAsynXXXArray<waveformRecord, asynInt32Array, interruptCallbackInt32Array, epicsInt32> *pObj =
        (devAsynXXXArray<waveformRecord, asynInt32Array, interruptCallbackInt32Array, epicsInt32> *) drvPvt;
    pObj->interruptCallback(pasynUser, value, len);
}
static long initWfInt32Out(waveformRecord *pr) {
    new devAsynXXXArray<waveformRecord, asynInt32Array, interruptCallbackInt32Array, epicsInt32>
                       (pr, &pr->inp, menuFtypeLONG, menuFtypeULONG, true, asynInt32ArrayType, qrCallbackWfInt32Out, intCallbackWfInt32Out);
    return 0;
}

typedef struct analogDset { /* analog  dset */
    long          number;
    DEVSUPFUN     dev_report;
    DEVSUPFUN     init;
    long          (*init_record)(waveformRecord *prec);
    long          (*get_ioint_info)(int cmd, dbCommon *pr, IOSCANPVT *iopvt);
    long          (*processCommon)(dbCommon *pr);/*(0)=>(success ) */
    DEVSUPFUN     special_linconv;
} analogDset;

#define MAKE_DSET(DSET_NAME, INIT_FUNC, GET_INFO_FUNC, PROC_FUNC) \
            analogDset (DSET_NAME) = {6, 0, 0, INIT_FUNC, GET_INFO_FUNC, PROC_FUNC, 0}; \
            epicsExportAddress(dset, DSET_NAME);

//MAKE_DSET(asynInt32ArrayWfIn,  initWfInt32In,  getInfoWfInt32In,  processWfInt32In);
MAKE_DSET(asynInt32ArrayWfOut, initWfInt32Out, getInfoWfInt32Out, processWfInt32Out);

}
