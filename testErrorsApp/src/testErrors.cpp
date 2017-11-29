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
#include <epicsEvent.h>
#include <iocsh.h>
#include <alarm.h>
#include <alarmString.h>

#include "testErrors.h"
#include <epicsExport.h>

static const char *driverName="testErrors";

#define UINT32_DIGITAL_MASK 0xFFFFFFFF

static const char *statusEnumStrings[MAX_STATUS_ENUMS] = {
    "asynSuccess",
    "asynTimeout",
    "asynOverFlow",
    "asynError",
    "asynDisconnect",
    "asynDisable"
};
static int statusEnumValues[MAX_STATUS_ENUMS]     = {0, 1, 2, 3, 4, 5};
static int statusEnumSeverities[MAX_STATUS_ENUMS] = {0, 2, 1, 2, 3, 3};

static const char *allInt32EnumStrings[MAX_INT32_ENUMS] = {
    "Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight",
    "Nine", "Ten", "Eleven", "Twelve", "Thirteen", "Fourteen", "Fifteen"
};
static int allInt32EnumValues[MAX_INT32_ENUMS]     =  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
static int allInt32EnumSeverities[MAX_INT32_ENUMS] =  {0, 0, 0, 0, 1, 1, 1, 1, 1, 2,  2,  2,  2,  3,  3,  3};

static const char *allUInt32EnumStrings[MAX_UINT32_ENUMS] = {
    "Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven"
};
static int allUInt32EnumValues[MAX_UINT32_ENUMS]     =  {0, 1, 2, 3, 4, 5, 6, 7};
static int allUInt32EnumSeverities[MAX_UINT32_ENUMS] =  {0, 0, 1, 1, 2, 2, 3, 3};

static void callbackTask(void *drvPvt);


/** Constructor for the testErrors class.
  * Calls constructor for the asynPortDriver base class.
  * \param[in] portName The name of the asyn port driver to be created. */
testErrors::testErrors(const char *portName, int canBlock) 
   : asynPortDriver(portName, 
                    1, /* maxAddr */ 
                     /* Interface mask */
                    asynInt32Mask       | asynFloat64Mask    | asynUInt32DigitalMask | asynOctetMask | 
                      asynInt8ArrayMask | asynInt16ArrayMask | asynInt32ArrayMask    | asynFloat32ArrayMask | asynFloat64ArrayMask |
                      asynOptionMask    | asynEnumMask       | asynDrvUserMask,
                    /* Interrupt mask */
                    asynInt32Mask       | asynFloat64Mask    | asynUInt32DigitalMask | asynOctetMask | 
                      asynInt8ArrayMask | asynInt16ArrayMask | asynInt32ArrayMask    | asynFloat32ArrayMask | asynFloat64ArrayMask |
                      asynEnumMask,
                    canBlock ? ASYN_CANBLOCK : 0, /* asynFlags */
                    1, /* Autoconnect */
                    0, /* Default priority */
                    0) /* Default stack size*/    
{
    asynStatus status;
    int i;
    const char *functionName = "testErrors";

    createParam(P_StatusReturnString,               asynParamInt32,         &P_StatusReturn);
    createParam(P_AlarmStatusString,                asynParamInt32,         &P_AlarmStatus);
    createParam(P_AlarmSeverityString,              asynParamInt32,         &P_AlarmSeverity);
    createParam(P_EnumOrderString,                  asynParamInt32,         &P_EnumOrder);
    createParam(P_DoUpdateString,                   asynParamInt32,         &P_DoUpdate);
    createParam(P_Int32ValueString,                 asynParamInt32,         &P_Int32Value);
    createParam(P_BinaryInt32ValueString,           asynParamInt32,         &P_BinaryInt32Value);
    createParam(P_MultibitInt32ValueString,         asynParamInt32,         &P_MultibitInt32Value);
    createParam(P_Float64ValueString,               asynParamFloat64,       &P_Float64Value);
    createParam(P_UInt32DigitalValueString,         asynParamUInt32Digital, &P_UInt32DigitalValue);
    createParam(P_BinaryUInt32DigitalValueString,   asynParamUInt32Digital, &P_BinaryUInt32DigitalValue);
    createParam(P_MultibitUInt32DigitalValueString, asynParamUInt32Digital, &P_MultibitUInt32DigitalValue);
    createParam(P_OctetValueString,                 asynParamOctet,         &P_OctetValue);
    createParam(P_Int8ArrayValueString,             asynParamInt8Array,     &P_Int8ArrayValue);
    createParam(P_Int16ArrayValueString,            asynParamInt16Array,    &P_Int16ArrayValue);
    createParam(P_Int32ArrayValueString,            asynParamInt32Array,    &P_Int32ArrayValue);
    createParam(P_Float32ArrayValueString,          asynParamFloat32Array,  &P_Float32ArrayValue);
    createParam(P_Float64ArrayValueString,          asynParamFloat64Array,  &P_Float64ArrayValue);
    
    for (i=0; i<MAX_INT32_ENUMS; i++) {
        int32EnumStrings_[i] = (char*)calloc(MAX_ENUM_STRING_SIZE, sizeof(char));
    }
    for (i=0; i<MAX_UINT32_ENUMS; i++) {
        uint32EnumStrings_[i] = (char*)calloc(MAX_ENUM_STRING_SIZE, sizeof(char));
    }
    setIntegerParam(P_StatusReturn,       asynSuccess);
    setIntegerParam(P_Int32Value,         0);
    setIntegerParam(P_BinaryInt32Value,   0);
    setIntegerParam(P_MultibitInt32Value, 0);
    setDoubleParam(P_Float64Value,        0.0);
    setIntegerParam(P_EnumOrder,          0);
    // Comment or uncomment the following line to test initializing asynOctet input and output records
    setStringParam(P_OctetValue,         "0.0");
    setEnums();
    // Need to force callbacks with the interruptMask once 
    setUIntDigitalParam(P_UInt32DigitalValue,         (epicsUInt32)0x0, 0xFFFFFFFF, 0xFFFFFFFF);
    setUIntDigitalParam(P_BinaryUInt32DigitalValue,   (epicsUInt32)0x0, 0xFFFFFFFF, 0xFFFFFFFF);
    setUIntDigitalParam(P_MultibitUInt32DigitalValue, (epicsUInt32)0x0, 0xFFFFFFFF, 0xFFFFFFFF);
    
    eventId_ = epicsEventCreate(epicsEventEmpty);
    
    /* Create the thread that updates values in the background */
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
    epicsInt32 alarmStatus;
    epicsInt32 alarmSeverity;
    epicsInt32 itemp;
    epicsInt32 iVal;
    epicsUInt32 uiVal;
    epicsFloat64 dVal;
    int i;
    char octetValue[20];
    
    lock();
    /* Loop forever */    
    while (1) {
        unlock();
        (void)epicsEventWait(eventId_);
        lock();
        updateTimeStamp();
        getIntegerParam(P_StatusReturn, &itemp); currentStatus = (asynStatus)itemp;
        getIntegerParam(P_AlarmStatus, &alarmStatus);
        getIntegerParam(P_AlarmSeverity, &alarmSeverity);

        getIntegerParam(P_Int32Value, &iVal);
        iVal++;
        if (iVal > 64) iVal=0;
        setIntegerParam(      P_Int32Value, iVal);
        setParamStatus(       P_Int32Value, currentStatus);
        setParamAlarmStatus(  P_Int32Value, alarmStatus);
        setParamAlarmSeverity(P_Int32Value, alarmSeverity);

        getIntegerParam(P_BinaryInt32Value, &iVal);
        iVal++;
        if (iVal > 1) iVal=0;
        setIntegerParam(      P_BinaryInt32Value, iVal);
        setParamStatus(       P_BinaryInt32Value, currentStatus);
        setParamAlarmStatus(  P_BinaryInt32Value, alarmStatus);
        setParamAlarmSeverity(P_BinaryInt32Value, alarmSeverity);

        getIntegerParam(P_MultibitInt32Value, &iVal);
        iVal++;
        if (iVal > MAX_INT32_ENUMS-1) iVal=0;
        setIntegerParam(      P_MultibitInt32Value, iVal);
        setParamStatus(       P_MultibitInt32Value, currentStatus);
        setParamAlarmStatus(  P_MultibitInt32Value, alarmStatus);
        setParamAlarmSeverity(P_MultibitInt32Value, alarmSeverity);

        getUIntDigitalParam(P_UInt32DigitalValue, &uiVal, UINT32_DIGITAL_MASK);
        uiVal++;
        if (uiVal > 64) uiVal=0;
        setUIntDigitalParam(  P_UInt32DigitalValue, uiVal, UINT32_DIGITAL_MASK);
        setParamStatus(       P_UInt32DigitalValue, currentStatus);
        setParamAlarmStatus(  P_UInt32DigitalValue, alarmStatus);
        setParamAlarmSeverity(P_UInt32DigitalValue, alarmSeverity);

        getUIntDigitalParam(P_BinaryUInt32DigitalValue, &uiVal, UINT32_DIGITAL_MASK);
        uiVal++;
        if (uiVal > 1) uiVal=0;
        setUIntDigitalParam(  P_BinaryUInt32DigitalValue, uiVal, UINT32_DIGITAL_MASK);
        setParamStatus(       P_BinaryUInt32DigitalValue, currentStatus);
        setParamAlarmStatus(  P_BinaryUInt32DigitalValue, alarmStatus);
        setParamAlarmSeverity(P_BinaryUInt32DigitalValue, alarmSeverity);

        getUIntDigitalParam(P_MultibitUInt32DigitalValue, &uiVal, UINT32_DIGITAL_MASK);
        uiVal++;
        if (uiVal > MAX_UINT32_ENUMS-1) uiVal=0;
        setUIntDigitalParam(  P_MultibitUInt32DigitalValue, uiVal, UINT32_DIGITAL_MASK);
        setParamStatus(       P_MultibitUInt32DigitalValue, currentStatus);
        setParamAlarmStatus(  P_MultibitUInt32DigitalValue, alarmStatus);
        setParamAlarmSeverity(P_MultibitUInt32DigitalValue, alarmSeverity);

        getDoubleParam(P_Float64Value, &dVal);
        dVal += 0.1;
        setDoubleParam(       P_Float64Value, dVal);
        setParamStatus(       P_Float64Value, currentStatus);
        setParamAlarmStatus(  P_Float64Value, alarmStatus);
        setParamAlarmSeverity(P_Float64Value, alarmSeverity);

        sprintf(octetValue, "%.1f", dVal); 
        setStringParam(       P_OctetValue, octetValue);
        setParamStatus(       P_OctetValue, currentStatus);
        setParamAlarmStatus(  P_OctetValue, alarmStatus);
        setParamAlarmSeverity(P_OctetValue, alarmSeverity);

        for (i=0; i<MAX_ARRAY_POINTS; i++) {
            int8ArrayValue_[i]    = iVal;
            int16ArrayValue_[i]   = iVal;
            int32ArrayValue_[i]   = iVal;
            float32ArrayValue_[i] = (epicsFloat32)dVal;
            float64ArrayValue_[i] = dVal;
        }
        callParamCallbacks();
        setParamStatus(       P_Int8ArrayValue,    currentStatus);
        setParamAlarmStatus(  P_Int8ArrayValue,    alarmStatus);
        setParamAlarmSeverity(P_Int8ArrayValue,    alarmSeverity);
        setParamStatus(       P_Int16ArrayValue,   currentStatus);
        setParamAlarmStatus(  P_Int16ArrayValue,   alarmStatus);
        setParamAlarmSeverity(P_Int16ArrayValue,   alarmSeverity);
        setParamStatus(       P_Int32ArrayValue,   currentStatus);
        setParamAlarmStatus(  P_Int32ArrayValue,   alarmStatus);
        setParamAlarmSeverity(P_Int32ArrayValue,   alarmSeverity);
        setParamStatus(       P_Float32ArrayValue, currentStatus);
        setParamAlarmStatus(  P_Float32ArrayValue, alarmStatus);
        setParamAlarmSeverity(P_Float32ArrayValue, alarmSeverity);
        setParamStatus(       P_Float64ArrayValue, currentStatus);
        setParamAlarmStatus(  P_Float64ArrayValue, alarmStatus);
        setParamAlarmSeverity(P_Float64ArrayValue, alarmSeverity);
        doCallbacksInt8Array(int8ArrayValue_,       MAX_ARRAY_POINTS, P_Int8ArrayValue,    0);
        doCallbacksInt16Array(int16ArrayValue_,     MAX_ARRAY_POINTS, P_Int16ArrayValue,   0);
        doCallbacksInt32Array(int32ArrayValue_,     MAX_ARRAY_POINTS, P_Int32ArrayValue,   0);
        doCallbacksFloat32Array(float32ArrayValue_, MAX_ARRAY_POINTS, P_Float32ArrayValue, 0);
        doCallbacksFloat64Array(float64ArrayValue_, MAX_ARRAY_POINTS, P_Float64ArrayValue, 0);
    }
}

void testErrors::setEnums()
{
    epicsInt32 order, offset=0, dir=1, i, j;
    
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
    doCallbacksEnum(int32EnumStrings_,  int32EnumValues_,  int32EnumSeverities_,  
                    MAX_INT32_ENUMS,  P_BinaryInt32Value,   0);
    doCallbacksEnum(int32EnumStrings_,  int32EnumValues_,  int32EnumSeverities_,  
                    MAX_INT32_ENUMS,  P_MultibitInt32Value, 0);
    doCallbacksEnum(uint32EnumStrings_, uint32EnumValues_, uint32EnumSeverities_, 
                    MAX_UINT32_ENUMS, P_BinaryUInt32DigitalValue,   0);
    doCallbacksEnum(uint32EnumStrings_, uint32EnumValues_, uint32EnumSeverities_, 
                    MAX_UINT32_ENUMS, P_MultibitUInt32DigitalValue, 0);
}

asynStatus testErrors::setStatusAndSeverity(asynUser *pasynUser)
{
    int status;
    
    getIntegerParam(P_StatusReturn, &status);
    getIntegerParam(P_AlarmStatus, &pasynUser->alarmStatus);
    getIntegerParam(P_AlarmSeverity, &pasynUser->alarmSeverity);
    setParamStatus(pasynUser->reason, (asynStatus) status);
    setParamAlarmStatus(pasynUser->reason, pasynUser->alarmStatus);
    setParamAlarmSeverity(pasynUser->reason, pasynUser->alarmSeverity);
    return (asynStatus) status;
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

    /* Fetch the parameter string name for use in debugging */
    getParamName(function, &paramName);

    /* Set the parameter value in the parameter library. */
    setIntegerParam(function, value);

    if (function == P_DoUpdate) {
        epicsEventSignal(eventId_);
    }
    else if (function == P_StatusReturn) {
    }
    else if (function == P_EnumOrder) {
        setEnums();
    }
    else {
        /* Set the parameter status in the parameter library except for the above commands which always return OK */
        status = setStatusAndSeverity(pasynUser);
    }
    
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

    /* Fetch the parameter string name for use in debugging */
    getParamName(function, &paramName);

    /* Set the parameter in the parameter library. */
    setDoubleParam(function, value);

    /* Set the parameter status in the parameter library */
    status = setStatusAndSeverity(pasynUser);

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

    /* Fetch the parameter string name for use in debugging */
    getParamName(function, &paramName);

    /* Set the parameter value in the parameter library. */
    setUIntDigitalParam(function, value, mask);

    /* Set the parameter status in the parameter library */
    status = setStatusAndSeverity(pasynUser);
    
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
    const char *paramName;
    const char *functionName = "writeOctet";

    /* Fetch the parameter string name for use in debugging */
    getParamName(function, &paramName);

    /* Set the parameter in the parameter library. */
    setStringParam(function, (char *)value);
    /* Set the parameter status in the parameter library */
    status = setStatusAndSeverity(pasynUser);

     /* Do callbacks so higher layers see any changes */
    callParamCallbacks();

    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, name=%s, value=%s", 
                  driverName, functionName, status, function, paramName, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, name=%s, value=%s\n", 
              driverName, functionName, function, paramName, value);
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
    else if (function == P_AlarmStatus) {
        for (i=0; ((i<ALARM_NSTATUS) && (i<nElements)); i++) {
            if (strings[i]) free(strings[i]);
            strings[i] = epicsStrDup(epicsAlarmConditionStrings[i]);
            values[i] = i;
            severities[i] = NO_ALARM;
        }
    }
    else if (function == P_AlarmSeverity) {
        for (i=0; ((i<ALARM_NSEV) && (i<nElements)); i++) {
            if (strings[i]) free(strings[i]);
            strings[i] = epicsStrDup(epicsAlarmSeverityStrings[i]);
            values[i] = i;
            severities[i] = NO_ALARM;
        }
    }
    else if ((function == P_Int32Value)       ||
             (function == P_BinaryInt32Value) ||   
             (function == P_MultibitInt32Value)) {
        for (i=0; ((i<MAX_INT32_ENUMS) && (i<nElements)); i++) {
            if (strings[i]) free(strings[i]);
            strings[i] = epicsStrDup(int32EnumStrings_[i]);
            values[i] = int32EnumValues_[i];
            severities[i] = int32EnumSeverities_[i];
        }
    }
    else if ((function == P_UInt32DigitalValue)       ||
             (function == P_BinaryUInt32DigitalValue) ||
             (function == P_MultibitUInt32DigitalValue)) {
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

asynStatus testErrors::readOption(asynUser *pasynUser, const char *key, char *value, int maxChars)
{
    asynStatus status = asynSuccess;
    
    strcpy(value, "");
    if (strcmp(key, "key1") == 0) strncpy(value, "value1", maxChars);
    else if (strcmp(key, "key2") == 0) strncpy(value, "value2", maxChars);
    else status = asynError;
    asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
        "testErrors::readOption, key=%s, value=%s, status=%d\n",
        key, value, status);
    return status;
}

asynStatus testErrors::writeOption(asynUser *pasynUser, const char *key, const char *value)
{
    asynStatus status = asynSuccess;
    
    asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
        "testErrors::writeOption, key=%s, value=%s\n",
        key, value);
    return status;
}


template <typename epicsType> 
asynStatus testErrors::doReadArray(asynUser *pasynUser, epicsType *value, 
                                   size_t nElements, size_t *nIn, int paramIndex, epicsType *pValue)
{
    int function = pasynUser->reason;
    size_t ncopy = MAX_ARRAY_POINTS;
    epicsInt32 status = asynSuccess;
    epicsTimeStamp timestamp;
    const char *functionName = "doReadArray";

    /* Get the current timestamp */
    getTimeStamp(&timestamp);
    pasynUser->timestamp = timestamp;

    /* Set the parameter status in the parameter library */
    status = setStatusAndSeverity(pasynUser);

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
    return (asynStatus)status;
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
int testErrorsConfigure(const char *portName, int canBlock)
{
    new testErrors(portName, canBlock);
    return asynSuccess;
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName", iocshArgString};
static const iocshArg initArg1 = { "canBlock", iocshArgInt};
static const iocshArg * const initArgs[] = {&initArg0, &initArg1};
static const iocshFuncDef initFuncDef = {"testErrorsConfigure", 2, initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    testErrorsConfigure(args[0].sval, args[1].ival);
}

void testErrorsRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(testErrorsRegister);

}

