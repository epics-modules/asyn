/*
 * testArrayRingBuffer.h
 * 
 * Asyn driver that inherits from the asynPortDriver class to test using ring buffers with devAsynXXXArray.
 *
 * Author: Mark Rivers
 *
 * Created October 24, 2014
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <epicsTypes.h>
#include <epicsThread.h>
#include <epicsEvent.h>
#include <iocsh.h>

#include <asynPortDriver.h>

#include <epicsExport.h>

static const char *driverName="testArrayRingBuffer";

/* These are the drvInfo strings that are used to identify the parameters.
 * They are used by asyn clients, including standard asyn device support */
#define P_RunStopString            "RUN_STOP"            /* asynInt32,    r/w */
#define P_MaxArrayLengthString     "MAX_ARRAY_LENGTH"    /* asynInt32,    r/o */
#define P_ArrayLengthString        "ARRAY_LENGTH"        /* asynInt32,    r/w */
#define P_LoopDelayString          "LOOP_DELAY"          /* asynFloat64,  r/w */
#define P_BurstLengthString        "BURST_LENGTH"        /* asynInt32,    r/w */
#define P_BurstDelayString         "BURST_DELAY"         /* asynFloat64,  r/w */
#define P_ScalarDataString         "SCALAR_DATA"         /* asynInt32,    r/w */
#define P_ArrayDataString          "ARRAY_DATA"          /* asynInt32Array,  r/w */

class testArrayRingBuffer : public asynPortDriver {
public:
    testArrayRingBuffer(const char *portName, int maxArrayLength);
                 
    /* These are the methods that we override from asynPortDriver */
    virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
    virtual asynStatus readInt32Array(asynUser *pasynUser, epicsInt32 *value,
                                        size_t nElements, size_t *nIn);

    /* These are the methods that are new to this class */
    void arrayGenTask(void);

protected:
    /** Values used for pasynUser->reason, and indexes into the parameter library. */
    int P_RunStop;
    #define FIRST_COMMAND P_RunStop
    int P_MaxArrayLength;
    int P_ArrayLength;
    int P_LoopDelay;
    int P_BurstLength;
    int P_BurstDelay;
    int P_ScalarData;
    int P_ArrayData;
    #define LAST_COMMAND P_ArrayData
 
private:
    /* Our data */
    epicsEventId eventId_;
    epicsInt32 *pData_;
};

#define NUM_PARAMS (&LAST_COMMAND - &FIRST_COMMAND + 1)

void arrayGenTaskC(void *drvPvt)
{
    testArrayRingBuffer *pPvt = (testArrayRingBuffer *)drvPvt;
    pPvt->arrayGenTask();
}

/** Constructor for the testArrayRingBuffer class.
  * Calls constructor for the asynPortDriver base class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] maxArrayLength The maximum  number of points in the volt and time arrays */
testArrayRingBuffer::testArrayRingBuffer(const char *portName, int maxArrayLength) 
   : asynPortDriver(portName, 
                    1, /* maxAddr */ 
                    (int)NUM_PARAMS,
                    asynInt32Mask | asynFloat64Mask | asynInt32ArrayMask | asynDrvUserMask, /* Interface mask */
                    asynInt32Mask | asynFloat64Mask | asynInt32ArrayMask,                   /* Interrupt mask */
                    0, /* asynFlags.  This driver does not block and it is not multi-device, so flag is 0 */
                    1, /* Autoconnect */
                    0, /* Default priority */
                    0) /* Default stack size*/    
{
    asynStatus status;
    const char *functionName = "testArrayRingBuffer";

    /* Make sure maxArrayLength is positive */
    if (maxArrayLength < 1) maxArrayLength = 10;
    
    /* Allocate the waveform array */
    pData_ = (epicsInt32 *)calloc(maxArrayLength, sizeof(epicsInt32));
    
    eventId_ = epicsEventCreate(epicsEventEmpty);
    createParam(P_RunStopString,            asynParamInt32,         &P_RunStop);
    createParam(P_MaxArrayLengthString,     asynParamInt32,         &P_MaxArrayLength);
    createParam(P_ArrayLengthString,        asynParamInt32,         &P_ArrayLength);
    createParam(P_LoopDelayString,          asynParamFloat64,       &P_LoopDelay);
    createParam(P_BurstLengthString,        asynParamInt32,         &P_BurstLength);
    createParam(P_BurstDelayString,         asynParamFloat64,       &P_BurstDelay);
    createParam(P_ScalarDataString,         asynParamInt32,         &P_ScalarData);
    createParam(P_ArrayDataString,          asynParamInt32Array,    &P_ArrayData);
    
    /* Set the initial values of some parameters */
    setIntegerParam(P_MaxArrayLength,    maxArrayLength);
    setIntegerParam(P_ArrayLength,       maxArrayLength);
    
    /* Create the thread that does the array callbacks in the background */
    status = (asynStatus)(epicsThreadCreate("testArrayRingBufferTask",
                          epicsThreadPriorityMedium,
                          epicsThreadGetStackSize(epicsThreadStackMedium),
                          (EPICSTHREADFUNC)::arrayGenTaskC,
                          this) == NULL);
    if (status) {
        printf("%s::%s: epicsThreadCreate failure\n", driverName, functionName);
        return;
    }
}



/** Array generation ask that runs as a separate thread.  When the P_RunStop parameter is set to 1
  * it periodically generates a burst of arrays. */
void testArrayRingBuffer::arrayGenTask(void)
{
    double loopDelay;
    int runStop; 
    int i, j;
    int burstLength;
    double burstDelay;
    int maxArrayLength;
    int arrayLength;
    
    lock();
    /* Loop forever */ 
    getIntegerParam(P_MaxArrayLength, &maxArrayLength);   
    while (1) {
        getDoubleParam(P_LoopDelay, &loopDelay);
        getDoubleParam(P_BurstDelay, &burstDelay);
        getIntegerParam(P_RunStop, &runStop);
        // Release the lock while we wait for a command to start or wait for updateTime
        unlock();
        if (runStop) epicsEventWaitWithTimeout(eventId_, loopDelay);
        else         epicsEventWait(eventId_);
        // Take the lock again
        lock(); 
        /* runStop could have changed while we were waiting */
        getIntegerParam(P_RunStop, &runStop);
        if (!runStop) continue;
        getIntegerParam(P_ArrayLength, &arrayLength);
        if (arrayLength > maxArrayLength) {
            arrayLength = maxArrayLength;
            setIntegerParam(P_ArrayLength, arrayLength);
        }
        getIntegerParam(P_BurstLength, &burstLength);
        for (i=0; i<burstLength; i++) {
            for (j=0; j<arrayLength; j++) {
                pData_[j] = i;
            }
            setIntegerParam(P_ScalarData, i);
            callParamCallbacks();
            doCallbacksInt32Array(pData_, arrayLength, P_ArrayData, 0);
            if (burstDelay > 0.0) 
                epicsThreadSleep(burstDelay);
        }
    }
}

/** Called when asyn clients call pasynInt32->write().
  * This function sends a signal to the arrayGenTask thread if the value of P_RunStop has changed.
  * For all parameters it sets the value in the parameter library and calls any registered callbacks..
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus testArrayRingBuffer::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *paramName;
    const char* functionName = "writeInt32";

    /* Set the parameter in the parameter library. */
    status = (asynStatus) setIntegerParam(function, value);
    
    /* Fetch the parameter string name for possible use in debugging */
    getParamName(function, &paramName);

    if (function == P_RunStop) {
        if (value) epicsEventSignal(eventId_);
    } 
    else {
        /* All other parameters just get set in parameter list, no need to
         * act on them here */
    }
    
    /* Do callbacks so higher layers see any changes */
    status = (asynStatus) callParamCallbacks();
    
    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, name=%s, value=%d", 
                  driverName, functionName, status, function, paramName, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, name=%s, value=%d\n", 
              driverName, functionName, function, paramName, value);
    return status;
}


/** Called when asyn clients call pasynInt32Array->read().
  * Returns the value of the P_ArrayData array.  
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Pointer to the array to read.
  * \param[in] nElements Number of elements to read.
  * \param[out] nIn Number of elements actually read. */
asynStatus testArrayRingBuffer::readInt32Array(asynUser *pasynUser, epicsInt32 *value, 
                                               size_t nElements, size_t *nIn)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    int nCopy;
    const char *functionName = "readFloat64Array";

    getIntegerParam(P_ArrayLength, &nCopy);
    if ((int)nElements < nCopy) nCopy = (int)nElements;
    if (function == P_ArrayData) {
        memcpy(value, pData_, nCopy*sizeof(epicsInt32));
        *nIn = nCopy;
    }
    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d", 
                  driverName, functionName, status, function);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d\n", 
              driverName, functionName, function);
    return status;
}
    
/* Configuration routine.  Called directly, or from the iocsh function below */

extern "C" {

/** EPICS iocsh callable function to call constructor for the testArrayRingBuffer class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] maxArrayLength The maximum  number of points in the volt and time arrays */
int testArrayRingBufferConfigure(const char *portName, int maxArrayLength)
{
    new testArrayRingBuffer(portName, maxArrayLength);
    return asynSuccess;
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName",iocshArgString};
static const iocshArg initArg1 = { "max array length",iocshArgInt};
static const iocshArg * const initArgs[] = {&initArg0,
                                            &initArg1};
static const iocshFuncDef initFuncDef = {"testArrayRingBufferConfigure",2,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    testArrayRingBufferConfigure(args[0].sval, args[1].ival);
}

void testArrayRingBufferRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(testArrayRingBufferRegister);

}
