/*
 * testOutputReadback.cpp
 * 
 * Asyn driver that inherits from the asynPortDriver class to test error handling in both normally scanned
 * and I/O Intr scanned records
 *
 * Author: Mark Rivers
 *
 * Created October 29, 2015
 */

#include <epicsTypes.h>
#include <iocsh.h>

#include "testOutputReadback.h"
#include <epicsExport.h>

// static const char *driverName="testOutputReadback";

#define UINT32_DIGITAL_MASK 0xFFFFFFFF

/** Constructor for the testOutputReadback class.
  * Calls constructor for the asynPortDriver base class.
  * \param[in] portName The name of the asyn port driver to be created. */
testOutputReadback::testOutputReadback(const char *portName, int initialReadStatus) 
   : asynPortDriver(portName, 
                    1, /* maxAddr */ 
                     /* Interface mask */
                    asynInt32Mask       | asynFloat64Mask    | asynUInt32DigitalMask | asynDrvUserMask,
                    /* Interrupt mask */
                    asynInt32Mask       | asynFloat64Mask    | asynUInt32DigitalMask,
                    0, /* asynFlags.  This driver does not block and it is not multi-device, so flag is 0 */
                    1, /* Autoconnect */
                    0, /* Default priority */
                    0) /* Default stack size*/,
     initialReadStatus_((asynStatus)initialReadStatus)   
{
    createParam(P_Int32ValueString,                 asynParamInt32,         &P_Int32Value);
    createParam(P_BinaryInt32ValueString,           asynParamInt32,         &P_BinaryInt32Value);
    createParam(P_MultibitInt32ValueString,         asynParamInt32,         &P_MultibitInt32Value);
    createParam(P_Float64ValueString,               asynParamFloat64,       &P_Float64Value);
    createParam(P_UInt32DigitalValueString,         asynParamUInt32Digital, &P_UInt32DigitalValue);
    createParam(P_BinaryUInt32DigitalValueString,   asynParamUInt32Digital, &P_BinaryUInt32DigitalValue);
    createParam(P_MultibitUInt32DigitalValueString, asynParamUInt32Digital, &P_MultibitUInt32DigitalValue);
    
    setIntegerParam    (P_Int32Value,                 7);
    setParamStatus     (P_Int32Value,                 asynSuccess);
    setIntegerParam    (P_BinaryInt32Value,           1);
    setParamStatus     (P_BinaryInt32Value,           asynSuccess);
    setIntegerParam    (P_MultibitInt32Value,         1);
    setParamStatus     (P_MultibitInt32Value,         asynSuccess);
    setDoubleParam     (P_Float64Value,               50.);
    setParamStatus     (P_Float64Value,               asynSuccess);
    setUIntDigitalParam(P_UInt32DigitalValue,         (epicsUInt32)0xFF, 0xFFFFFFFF, 0xFFFFFFFF);
    setParamStatus     (P_UInt32DigitalValue,         asynSuccess);
    setUIntDigitalParam(P_BinaryUInt32DigitalValue,   (epicsUInt32)0x1, 0xFFFFFFFF, 0xFFFFFFFF);
    setParamStatus     (P_BinaryUInt32DigitalValue,   asynSuccess);
    setUIntDigitalParam(P_MultibitUInt32DigitalValue, (epicsUInt32)0x2, 0xFFFFFFFF, 0xFFFFFFFF);
    setParamStatus     (P_BinaryUInt32DigitalValue,   asynSuccess);
    
}

asynStatus testOutputReadback::readInt32(asynUser *pasynUser, epicsInt32 *value)
{
    if (initialReadStatus_) 
        return initialReadStatus_;
    else
        return asynPortDriver::readInt32(pasynUser, value);
}

asynStatus testOutputReadback::readFloat64(asynUser *pasynUser, epicsFloat64 *value)
{
    if (initialReadStatus_) 
        return initialReadStatus_;
    else
        return asynPortDriver::readFloat64(pasynUser, value);
}

asynStatus testOutputReadback::readUInt32Digital(asynUser *pasynUser, epicsUInt32 *value, epicsUInt32 mask)
{
    if (initialReadStatus_) 
        return initialReadStatus_;
    else
        return asynPortDriver::readUInt32Digital(pasynUser, value, mask);
}

/* Configuration routine.  Called directly, or from the iocsh function below */

extern "C" {

/** EPICS iocsh callable function to call constructor for the testOutputReadback class.
  * \param[in] portName The name of the asyn port driver to be created. */
int testOutputReadbackConfigure(const char *portName, int initialReadStatus)
{
    new testOutputReadback(portName, initialReadStatus);
    return(asynSuccess);
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName",iocshArgString};
static const iocshArg initArg1 = { "intial read status",iocshArgInt};
static const iocshArg * const initArgs[] = {&initArg0, &initArg1};
static const iocshFuncDef initFuncDef = {"testOutputReadbackConfigure",2,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    testOutputReadbackConfigure(args[0].sval, args[1].ival);
}

void testOutputReadbackRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(testOutputReadbackRegister);

}

