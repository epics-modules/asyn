/*
 * testOutputCallback.cpp
 * 
 * Asyn driver that inherits from the asynPortDriver class to test output records using callbacks
 * The output records must have the info tag asyn:READBACK
 * It tests the following:
 *   Callbacks done in immediately in the writeXXX function
 *   Callbacks done asynchronously in another driver thread
 * Configuration parameters in the driver constructor control the following:
 *   If the driver is synchronous or asynchronous
 *   How many callbacks are done each time
 * It does these tests on the asynInt32, asynUInt32Digital, asynFloat64, and asynOctet interfaces
 * Author: Mark Rivers
 *
 * Created November 9, 2017
 */

#include <string.h>
#include <epicsThread.h>
#include <epicsEvent.h>
#include <epicsTypes.h>
#include <iocsh.h>

#include <asynPortDriver.h>

#include <epicsExport.h>
#include "testOutputCallback.h"

//static const char *driverName="testOutputCallback";

#define UINT32_DIGITAL_MASK 0xFFFFFFFF

static void callbackThreadC(void *pPvt)
{
    testOutputCallback *p = (testOutputCallback*)pPvt;
    p->callbackThread();
}

/** Constructor for the testOutputCallback class.
  * Calls constructor for the asynPortDriver base class.
  * \param[in] portName The name of the asyn port driver to be created. */
testOutputCallback::testOutputCallback(const char *portName, int canBlock) 
   : asynPortDriver(portName, 
                    1, /* maxAddr */ 
                     /* Interface mask */
                    asynInt32Mask | asynFloat64Mask | asynUInt32DigitalMask | asynOctetMask | asynDrvUserMask,
                    /* Interrupt mask */
                    asynInt32Mask | asynFloat64Mask | asynUInt32DigitalMask | asynOctetMask,
                    canBlock ? ASYN_CANBLOCK : 0, /* asynFlags.  canblock is passed to constructor and multi-device is 0 */
                    1, /* Autoconnect */
                    0, /* Default priority */
                    0) /* Default stack size*/,
     numCallbacks_(1), sleepTime_(0.)  
{
    createParam(P_Int32ValueString,                 asynParamInt32,         &P_Int32Value);
    createParam(P_Int32BinaryValueString,           asynParamInt32,         &P_Int32BinaryValue);
    createParam(P_UInt32DigitalValueString,         asynParamUInt32Digital, &P_UInt32DigitalValue);
    createParam(P_Float64ValueString,               asynParamFloat64,       &P_Float64Value);
    createParam(P_OctetValueString,                 asynParamOctet,         &P_OctetValue);
    createParam(P_NumCallbacksString,               asynParamInt32,         &P_NumCallbacks);
    createParam(P_SleepTimeString,                  asynParamFloat64,       &P_SleepTime);
    createParam(P_TriggerCallbacksString,           asynParamInt32,         &P_TriggerCallbacks);
    
    setIntegerParam(P_NumCallbacks, numCallbacks_);
    setDoubleParam(P_SleepTime, sleepTime_);
    
    callbackEvent_ = epicsEventCreate(epicsEventEmpty);
    epicsThreadCreate("callbackThread",
        epicsThreadPriorityMedium,
        epicsThreadGetStackSize(epicsThreadStackSmall),
        (EPICSTHREADFUNC)callbackThreadC, this);
}

asynStatus testOutputCallback::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int function=pasynUser->reason;

    setIntegerParam(function, value);

    if (function == P_Int32Value) {
        doInt32Callbacks();
    }
    else if (function == P_Int32BinaryValue) {
        doInt32BinaryCallbacks();
    }
    else if (function == P_NumCallbacks) {
        numCallbacks_ = value;
    }
    else if (function == P_TriggerCallbacks) {
        epicsEventSignal(callbackEvent_);
    }
    return asynSuccess;
}

asynStatus testOutputCallback::writeUInt32Digital(asynUser *pasynUser, epicsUInt32 value, epicsUInt32 mask)
{
    int function=pasynUser->reason;
    setUIntDigitalParam(function, value, mask);
    doUInt32DigitalCallbacks();
    return asynSuccess;
}

asynStatus testOutputCallback::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
    int function=pasynUser->reason;
    setDoubleParam(function, value);
    if (function == P_Float64Value) {
        doFloat64Callbacks();
    } 
    else if (function == P_SleepTime) {
        sleepTime_ = value;
    }
    return asynSuccess;
}

asynStatus testOutputCallback::writeOctet(asynUser *pasynUser, const char *value, size_t maxChars, size_t *nActual)
{
    int function=pasynUser->reason;
    setStringParam(function, value);
    *nActual = strlen(value);
    doOctetCallbacks();
    return asynSuccess;
}

void testOutputCallback::doInt32Callbacks()
{
    epicsInt32 value;
    for (int i=0; i<numCallbacks_; i++) {
        getIntegerParam(P_Int32Value, &value);
        setIntegerParam(P_Int32Value, value+1);
        callParamCallbacks();
        if (sleepTime_ > 0) epicsThreadSleep(sleepTime_);
    }
}

void testOutputCallback::doInt32BinaryCallbacks()
{
    epicsInt32 value;
    for (int i=0; i<numCallbacks_; i++) {
        getIntegerParam(P_Int32BinaryValue, &value);
        value = value ? 0:1;
        setIntegerParam(P_Int32BinaryValue, value);
        callParamCallbacks();
        if (sleepTime_ > 0) epicsThreadSleep(sleepTime_);
    }
}

void testOutputCallback::doUInt32DigitalCallbacks()
{
    epicsUInt32 value;
    for (int i=0; i<numCallbacks_; i++) {
        getUIntDigitalParam(P_UInt32DigitalValue, &value, UINT32_DIGITAL_MASK);
        setUIntDigitalParam(P_UInt32DigitalValue, value+1, UINT32_DIGITAL_MASK);
        callParamCallbacks();
        if (sleepTime_ > 0) epicsThreadSleep(sleepTime_);
    }
}

void testOutputCallback::doFloat64Callbacks()
{
    epicsFloat64 value;
    for (int i=0; i<numCallbacks_; i++) {
        getDoubleParam(P_Float64Value, &value);
        setDoubleParam(P_Float64Value, value+1);
        callParamCallbacks();
        if (sleepTime_ > 0) epicsThreadSleep(sleepTime_);
    }
}

void testOutputCallback::doOctetCallbacks()
{
    char value[100];
    static int counter;
    for (int i=0; i<numCallbacks_; i++) {
        sprintf(value, "Value=%d", ++counter);
        setStringParam(P_OctetValue, value);
        callParamCallbacks();
        if (sleepTime_ > 0) epicsThreadSleep(sleepTime_);
    }
}

void testOutputCallback::callbackThread()
{
    lock();
    while (1) {
        unlock();
        (void)epicsEventWait(callbackEvent_);
        lock();
        doInt32Callbacks();
        doInt32BinaryCallbacks();
        doUInt32DigitalCallbacks();
        doFloat64Callbacks();
        doOctetCallbacks();
    }
}


/* Configuration routine.  Called directly, or from the iocsh function below */
extern "C" {

/** EPICS iocsh callable function to call constructor for the testOutputCallback class.
  * \param[in] portName The name of the asyn port driver to be created. */
int testOutputCallbackConfigure(const char *portName, int canBlock)
{
    new testOutputCallback(portName, canBlock);
    return asynSuccess ;
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName", iocshArgString};
static const iocshArg initArg1 = { "canBlock", iocshArgInt};
static const iocshArg * const initArgs[] = {&initArg0, &initArg1};
static const iocshFuncDef initFuncDef = {"testOutputCallbackConfigure",2,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    testOutputCallbackConfigure(args[0].sval, args[1].ival);
}

void testOutputCallbackRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(testOutputCallbackRegister);

}

