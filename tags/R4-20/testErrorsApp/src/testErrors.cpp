/*
 * testErrors.cpp
 * 
 * Asyn driver that inherits from the asynPortDriver class to test error handling in both normally scanned
 * and I/O Intr scanned records
 *
 * Author: Mark Rivers
 *
 * Created April 29, 2012
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <epicsTypes.h>
#include <epicsTime.h>
#include <epicsThread.h>
#include <epicsString.h>
#include <iocsh.h>

#include "testErrors.h"
#include <epicsExport.h>

static const char *driverName="testErrors";

#define CALLBACK_PERIOD 0.5

static char *statusEnumStrings[MAX_STATUS_ENUMS] = {
    "asynSuccess",
    "asynTimeout",
    "asynOverFlow",
    "asynError",
    "asynDisconnect",
    "asynDisable"
};
static int statusEnumValues[MAX_STATUS_ENUMS]     = {0, 1, 2, 3, 4, 5};
static int statusEnumSeverities[MAX_STATUS_ENUMS] = {0, 2, 1, 2, 3, 3};

static char *allInt32EnumStrings[MAX_INT32_ENUMS] = {
    "Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight",
    "Nine", "Ten", "Eleven", "Twelve", "Thirteen", "Fourteen", "Fifteen"
};
static int allInt32EnumValues[MAX_INT32_ENUMS]     =  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
static int allInt32EnumSeverities[MAX_INT32_ENUMS] =  {0, 0, 0, 0, 1, 1, 1, 1, 1, 2,  2,  2,  2,  3,  3,  3};

static char *allUInt32EnumStrings[MAX_UINT32_ENUMS] =  {"Zero", "One", "Three"};
static int allUInt32EnumValues[MAX_UINT32_ENUMS]     = {0, 1, 3};
static int allUInt32EnumSeverities[MAX_UINT32_ENUMS] = {0, 1, 1};

static void callbackTask(void *drvPvt);


/** Constructor for the testErrors class.
  * Calls constructor for the asynPortDriver base class.
  * \param[in] portName The name of the asyn port driver to be created. */
testErrors::testErrors(const char *portName) 
   : asynPortDriver(portName, 
                    1, /* maxAddr */ 
                    NUM_PARAMS,
                     /* Interface mask */
                    asynInt32Mask       | asynFloat64Mask    | asynUInt32DigitalMask | asynOctetMask | 
                      asynInt8ArrayMask | asynInt16ArrayMask | asynInt32ArrayMask    | asynFloat32ArrayMask | asynFloat64ArrayMask |
                      asynEnumMask      | asynDrvUserMask,
                    /* Interrupt mask */
                    asynInt32Mask       | asynFloat64Mask    | asynUInt32DigitalMask | asynOctetMask | 
                      asynInt8ArrayMask | asynInt16ArrayMask | asynInt32ArrayMask    | asynFloat32ArrayMask | asynFloat64ArrayMask |
                      asynEnumMask,
                    0, /* asynFlags.  This driver does not block and it is not multi-device, so flag is 0 */
                    1, /* Autoconnect */
                    0, /* Default priority */
                    0) /* Default stack size*/    
{
    asynStatus status;
    int i;
    const char *functionName = "testErrors";

    createParam(P_StatusReturnString,       asynParamInt32,         &P_StatusReturn);
    createParam(P_EnumOrderString,          asynParamInt32,         &P_EnumOrder);
    createParam(P_Int32ValueString,         asynParamInt32,         &P_Int32Value);
    createParam(P_Float64ValueString,       asynParamFloat64,       &P_Float64Value);
    createParam(P_UInt32DigitalValueString, asynParamUInt32Digital, &P_UInt32DigitalValue);
    createParam(P_OctetValueString,         asynParamOctet,         &P_OctetValue);
    createParam(P_Int8ArrayValueString,     asynParamInt8Array,     &P_Int8ArrayValue);
    createParam(P_Int16ArrayValueString,    asynParamInt16Array,    &P_Int16ArrayValue);
    createParam(P_Int32ArrayValueString,    asynParamInt32Array,    &P_Int32ArrayValue);
    createParam(P_Float32ArrayValueString,  asynParamFloat32Array,  &P_Float32ArrayValue);
    createParam(P_Float64ArrayValueString,  asynParamFloat64Array,  &P_Float64ArrayValue);
    
    for (i=0; i<MAX_INT32_ENUMS; i++) {
        int32EnumStrings_[i] = (char*)calloc(MAX_ENUM_STRING_SIZE, sizeof(char));
    }
    for (i=0; i<MAX_UINT32_ENUMS; i++) {
        uint32EnumStrings_[i] = (char*)calloc(MAX_ENUM_STRING_SIZE, sizeof(char));
    }
    setIntegerParam(P_StatusReturn, asynSuccess);
    setIntegerParam(P_Int32Value, 0);
    setIntegerParam(P_EnumOrder, 0);
    setEnums();
    // Need to force callbacks with the interruptMask once 
    setUIntDigitalParam(P_UInt32DigitalValue, (epicsUInt32)0x0, 0xFFFFFFFF, 0xFFFFFFFF);
    
    /* Create the thread that computes the waveforms in the background */
    status = (asynStatus)(epicsThreadCreate("testErrorsTask",
                          epicsThreadPriorityMedium,
                          epicsThreadGetStackSize(epicsThreadStackMedium),
                          (EPICSTHREADFUNC)::callbackTask,
                          this) == NULL);
    if (status) {
        printf("%s:%s: epicsThreadCreate failure\n", driverName, functionName);
        return;
    }
}



static void callbackTask(void *drvPvt)
{
    testErrors *pPvt = (testErrors *)drvPvt;
    
    pPvt->callbackTask();
}

/** Callback task that runs as a separate thread. */
void testErrors::callbackTask(void)
{
    asynStatus currentStatus;
    epicsInt32 iVal;
    epicsFloat64 dVal;
    int i;
    char octetValue[20];
    
    /* Loop forever */    
    while (1) {
        lock();
        getIntegerParam(P_StatusReturn, (int*)&currentStatus);
        getIntegerParam(P_Int32Value, &iVal);
        iVal++;
        if (iVal > 15) iVal=0;
        setIntegerParam(P_Int32Value, iVal);
        setParamStatus(P_Int32Value, currentStatus);
        getDoubleParam(P_Float64Value, &dVal);
        dVal += 0.1;
        setDoubleParam(P_Float64Value, dVal);
        setParamStatus(P_Float64Value, currentStatus);
        sprintf(octetValue, "%.1f", dVal); 
        setParamStatus(P_UInt32DigitalValue, currentStatus);
        setStringParam(P_OctetValue, octetValue);
        setParamStatus(P_OctetValue, currentStatus);
        setParamStatus(P_Float64ArrayValue, currentStatus);
        for (i=0; i<MAX_ARRAY_POINTS; i++) {
            int8ArrayValue_[i]    = iVal;
            int16ArrayValue_[i]   = iVal;
            int32ArrayValue_[i]   = iVal;
            float32ArrayValue_[i] = dVal;
            float64ArrayValue_[i] = dVal;
        }
        callParamCallbacks();
        setParamStatus(P_Int8ArrayValue, currentStatus);
        doCallbacksInt8Array(int8ArrayValue_, MAX_ARRAY_POINTS, P_Int8ArrayValue, 0);
        setParamStatus(P_Int16ArrayValue, currentStatus);
        doCallbacksInt16Array(int16ArrayValue_, MAX_ARRAY_POINTS, P_Int16ArrayValue, 0);
        setParamStatus(P_Int32ArrayValue, currentStatus);
        doCallbacksInt32Array(int32ArrayValue_, MAX_ARRAY_POINTS, P_Int32ArrayValue, 0);
        setParamStatus(P_Float32ArrayValue, currentStatus);
        doCallbacksFloat32Array(float32ArrayValue_, MAX_ARRAY_POINTS, P_Float32ArrayValue, 0);
        setParamStatus(P_Float64ArrayValue, currentStatus);
        doCallbacksFloat64Array(float64ArrayValue_, MAX_ARRAY_POINTS, P_Float64ArrayValue, 0);
        unlock();
        epicsThreadSleep(CALLBACK_PERIOD);
    }
}

void testErrors::setEnums()
{
    int order, offset=0, dir=1, i, j;
    
    getIntegerParam(P_EnumOrder, &order);
    if (order != 0) {
        offset = MAX_INT32_ENUMS-1;
        dir = -1;
    }
    for (i=0, j=offset; i<MAX_INT32_ENUMS; i++, j+=dir) {
        strcpy(int32EnumStrings_[i],  allInt32EnumStrings[j]);
        int32EnumValues_[i]         = allInt32EnumValues[j];
        int32EnumSeverities_[i]     = allInt32EnumSeverities[j];
    }
    if (order != 0) {
        offset = MAX_UINT32_ENUMS-1;
    }
    for (i=0, j=offset; i<MAX_UINT32_ENUMS; i++, j+=dir) {
        strcpy(uint32EnumStrings_[i],  allUInt32EnumStrings[j]);
        uint32EnumValues_[i]         = allUInt32EnumValues[j];
        uint32EnumSeverities_[i]     = allUInt32EnumSeverities[j];
    }
    doCallbacksEnum(int32EnumStrings_,  int32EnumValues_,  int32EnumSeverities_,  MAX_INT32_ENUMS,  P_Int32Value,         0);
    doCallbacksEnum(uint32EnumStrings_, uint32EnumValues_, uint32EnumSeverities_, MAX_UINT32_ENUMS, P_UInt32DigitalValue, 0);
}

/** Called when asyn clients call pasynInt32->write().
  * This function sends a signal to the simTask thread if the value of P_Run has changed.
  * For all parameters it sets the value in the parameter library and calls any registered callbacks..
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus testErrors::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *paramName;
    const char* functionName = "writeInt32";

    /* Get the current error status */
    getIntegerParam(P_StatusReturn, (int*)&status);

    /* Fetch the parameter string name for use in debugging */
    getParamName(function, &paramName);

    /* Set the parameter value in the parameter library. */
    setIntegerParam(function, value);
    /* Set the parameter status in the parameter library except for P_StatusReturn which is always OK */
    if (function == P_StatusReturn) {
        status = asynSuccess;
    }
    else if (function == P_EnumOrder) {
        setEnums();
    }
    setParamStatus(function, status);
    
    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();
    
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

/** Called when asyn clients call pasynFloat64->write().
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus testErrors::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *paramName;
    const char* functionName = "writeFloat64";

    /* Get the current error status */
    getIntegerParam(P_StatusReturn, (int*)&status);

    /* Fetch the parameter string name for use in debugging */
    getParamName(function, &paramName);

    /* Set the parameter in the parameter library. */
    setDoubleParam(function, value);
    /* Set the parameter status in the parameter library. */
    setParamStatus(function, status);

    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();
    
    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, name=%s, value=%f", 
                  driverName, functionName, status, function, paramName, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, name=%s, value=%f\n", 
              driverName, functionName, function, paramName, value);
    return status;
}

/** Called when asyn clients call pasynUInt32D->write().
  * For all parameters it sets the value in the parameter library and calls any registered callbacks..
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus testErrors::writeUInt32Digital(asynUser *pasynUser, epicsUInt32 value, epicsUInt32 mask)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *paramName;
    const char* functionName = "writeUInt32D";

    /* Get the current error status */
    getIntegerParam(P_StatusReturn, (int*)&status);

    /* Fetch the parameter string name for use in debugging */
    getParamName(function, &paramName);

    /* Set the parameter value in the parameter library. */
    setUIntDigitalParam(function, value, mask);
    /* Set the parameter status in the parameter library. */
    setParamStatus(function, status);
    
    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();
    
    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, name=%s, value=0x%X", 
                  driverName, functionName, status, function, paramName, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, name=%s, value=0x%X\n", 
              driverName, functionName, function, paramName, value);
    return status;
}

/** Called when asyn clients call pasynOctet->write().
  * Simply sets the value in the parameter library and 
  * calls any registered callbacks for this pasynUser->reason and address.  
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Address of the string to write.
  * \param[in] nChars Number of characters to write.
  * \param[out] nActual Number of characters actually written. */
asynStatus testErrors::writeOctet(asynUser *pasynUser, const char *value, 
                                  size_t nChars, size_t *nActual)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *functionName = "writeOctet";

    /* Get the current error status */
    getIntegerParam(P_StatusReturn, (int*)&status);

    /* Set the parameter in the parameter library. */
    setStringParam(function, (char *)value);
    /* Set the parameter status in the parameter library. */
    setParamStatus(function, status);

     /* Do callbacks so higher layers see any changes */
    callParamCallbacks();

    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, value=%s", 
                  driverName, functionName, status, function, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, value=%s\n", 
              driverName, functionName, function, value);
    *nActual = nChars;
    return status;
}

asynStatus testErrors::readEnum(asynUser *pasynUser, char *strings[], int values[], int severities[], size_t nElements, size_t *nIn)
{
    int function = pasynUser->reason;
    size_t i;

    if (function == P_StatusReturn) {
        for (i=0; ((i<MAX_STATUS_ENUMS) && (i<nElements)); i++) {
            if (strings[i]) free(strings[i]);
            strings[i] = epicsStrDup(statusEnumStrings[i]);
            values[i] = statusEnumValues[i];
            severities[i] = statusEnumSeverities[i];
        }
    }
    else if (function == P_Int32Value) {
        for (i=0; ((i<MAX_INT32_ENUMS) && (i<nElements)); i++) {
            if (strings[i]) free(strings[i]);
            strings[i] = epicsStrDup(int32EnumStrings_[i]);
            values[i] = int32EnumValues_[i];
            severities[i] = int32EnumSeverities_[i];
        }
    }
    else if (function == P_UInt32DigitalValue) {
        for (i=0; ((i<MAX_UINT32_ENUMS) && (i<nElements)); i++) {
            if (strings[i]) free(strings[i]);
            strings[i] = epicsStrDup(uint32EnumStrings_[i]);
            values[i] = uint32EnumValues_[i];
            severities[i] = uint32EnumSeverities_[i];
        }
    }
    else {
        *nIn = 0;
        return asynError;
    }
    *nIn = i;
    return asynSuccess;   

}

template <typename epicsType> 
asynStatus testErrors::doReadArray(asynUser *pasynUser, epicsType *value, 
                                   size_t nElements, size_t *nIn, int paramIndex, epicsType *pValue)
{
    int function = pasynUser->reason;
    size_t ncopy = MAX_ARRAY_POINTS;
    asynStatus status = asynSuccess;
    const char *functionName = "doReadArray";

    /* Get the current error status */
    getIntegerParam(P_StatusReturn, (int*)&status);

    if (nElements < ncopy) ncopy = nElements;
    if (function == paramIndex) {
        memcpy(value, pValue, ncopy*sizeof(epicsType));
        *nIn = ncopy;
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
    
asynStatus testErrors::readInt8Array(asynUser *pasynUser, epicsInt8 *value, 
                                        size_t nElements, size_t *nIn)
{
    return doReadArray<epicsInt8>
        (pasynUser, value, nElements, nIn, P_Int8ArrayValue, int8ArrayValue_);
}

asynStatus testErrors::readInt16Array(asynUser *pasynUser, epicsInt16 *value, 
                                        size_t nElements, size_t *nIn)
{
    return doReadArray<epicsInt16>
        (pasynUser, value, nElements, nIn, P_Int16ArrayValue, int16ArrayValue_);
}

asynStatus testErrors::readInt32Array(asynUser *pasynUser, epicsInt32 *value, 
                                        size_t nElements, size_t *nIn)
{
    return doReadArray<epicsInt32>
        (pasynUser, value, nElements, nIn, P_Int32ArrayValue, int32ArrayValue_);
}

asynStatus testErrors::readFloat32Array(asynUser *pasynUser, epicsFloat32 *value, 
                                        size_t nElements, size_t *nIn)
{
    return doReadArray<epicsFloat32>
        (pasynUser, value, nElements, nIn, P_Float32ArrayValue, float32ArrayValue_);
}

asynStatus testErrors::readFloat64Array(asynUser *pasynUser, epicsFloat64 *value, 
                                        size_t nElements, size_t *nIn)
{
    return doReadArray<epicsFloat64>
        (pasynUser, value, nElements, nIn, P_Float64ArrayValue, float64ArrayValue_);
}
    


/* Configuration routine.  Called directly, or from the iocsh function below */

extern "C" {

/** EPICS iocsh callable function to call constructor for the testErrors class.
  * \param[in] portName The name of the asyn port driver to be created. */
int testErrorsConfigure(const char *portName)
{
    new testErrors(portName);
    return(asynSuccess);
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName",iocshArgString};
static const iocshArg * const initArgs[] = {&initArg0};
static const iocshFuncDef initFuncDef = {"testErrorsConfigure",1,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    testErrorsConfigure(args[0].sval);
}

void testErrorsRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(testErrorsRegister);

}
