/*
 * asynPortTest.cpp
 * 
 * Asyn driver that inherits from the asynPortDriver class to demonstrate its use.
 * It communicates with the echoServer driver.  
 * It sends and receives int32, float64 and octet values.
 *
 * Author: Mark Rivers
 *
 * Created June 16, 2010
 */

#include <string.h>
#include <ctype.h>

#include <iocsh.h>

#include "asynPortDriver.h"
#include "asynOctetSyncIO.h"
#include <epicsExport.h>

#define MAX_MESSAGE_SIZE 80
#define TIMEOUT 1.0

/* These are the drvInfo strings that are used to identify the parameters.
 * They are used by asyn clients, including standard asyn device support */
#define Int32DataString         "INT32_DATA"            /* asynInt32,    r/w */
#define Float64DataString       "FLOAT64_DATA"          /* asynFloat64,  r/w */
#define OctetDataString         "OCTET_DATA"            /* asynOctet,    r/w */

class asynPortTest : public asynPortDriver {
public:
    asynPortTest(const char *portName, const char *echoPortName);
                 
    /* These are the methods that we override from asynPortDriver */
    virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
    virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
    virtual asynStatus writeOctet(asynUser *pasynUser, const char *value, 
                                  size_t nChars, size_t *nActual);

protected:
    /** Values used for pasynUser->reason, and indexes into the parameter library. */
    int Int32Data;
    int Float64Data;
    int OctetData;
 
private:
    /* Our data */
    asynUser *pasynUserEcho;
    char writeBuffer[MAX_MESSAGE_SIZE];
    char readBuffer[MAX_MESSAGE_SIZE];
};


static const char *driverName="asynPortTest";

/** Called when asyn clients call pasynInt32->write().
  * This function converts the integer to a string and sends it to the echoServer.
  * It reads back the string response, converts it to a number, divides by 2 and calls
  * the callbacks with the new value. 
  * For all parameters it sets the value in the parameter library and calls any registered callbacks..
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus asynPortTest::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int function = pasynUser->reason;
    size_t nRead, nActual;
    int eomReason;
    int readValue;
    asynStatus status = asynSuccess;
    const char* functionName = "writeInt32";

    /* Set the parameter in the parameter library. */
    status = (asynStatus) setIntegerParam(function, value);
    
    if (function == Int32Data) {
        sprintf(writeBuffer, "%d", value);
        status = pasynOctetSyncIO->writeRead(pasynUserEcho, writeBuffer, strlen(writeBuffer), readBuffer, 
                                         sizeof(readBuffer), TIMEOUT, &nActual, &nRead, &eomReason);
        sscanf(readBuffer, "%d", &readValue);
        readValue /= 2;
        setIntegerParam(Int32Data, readValue);        
    } 
    
    /* Do callbacks so higher layers see any changes */
    status = (asynStatus) callParamCallbacks();
    
    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, value=%d", 
                  driverName, functionName, status, function, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, value=%d\n", 
              driverName, functionName, function, value);
    return status;
}

/** Called when asyn clients call pasynFloat64->write().
  * For all  parameters it  sets the value in the parameter library and calls any registered callbacks.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus asynPortTest::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    size_t nRead, nActual;
    int eomReason;
    double readValue;
    const char* functionName = "writeFloat64";

    /* Set the parameter in the parameter library. */
    status = (asynStatus) setDoubleParam(function, value);

    if (function == Float64Data) {
        sprintf(writeBuffer, "%f", value);
        status = pasynOctetSyncIO->writeRead(pasynUserEcho, writeBuffer, strlen(writeBuffer), readBuffer, 
                                         sizeof(readBuffer), TIMEOUT, &nActual, &nRead, &eomReason);
        sscanf(readBuffer, "%lf", &readValue);
        readValue /= 3;
        setDoubleParam(Float64Data, readValue);        
    }
        
    /* Do callbacks so higher layers see any changes */
    status = (asynStatus) callParamCallbacks();
    
    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, value=%f", 
                  driverName, functionName, status, function, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, value=%f\n", 
              driverName, functionName, function, value);
    return status;
}

/** Called when asyn clients call pasynOctet->write().
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Address of the string to write.
  * \param[in] nChars Number of characters to write.
  * \param[out] nActual Number of characters actually written. */
asynStatus asynPortTest::writeOctet(asynUser *pasynUser, const char *value, 
                                    size_t nChars, size_t *nActual)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    size_t nRead;
    int eomReason;
    int i;
    const char *functionName = "writeOctet";

    if (function == OctetData) {
        status = pasynOctetSyncIO->writeRead(pasynUserEcho, value, nChars, readBuffer, 
                                             sizeof(readBuffer), TIMEOUT, nActual, &nRead, &eomReason);
        /* Convert to upper case */
        for (i=0; i<(int)nRead; i++) readBuffer[i] = toupper(readBuffer[i]);
        status = (asynStatus)setStringParam(OctetData, readBuffer); 
    }

     /* Do callbacks so higher layers see any changes */
    status = (asynStatus)callParamCallbacks();

    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, value=%s", 
                  driverName, functionName, status, function, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, value=%s\n", 
              driverName, functionName, function, value);
    return status;
}


/** Constructor for the asynPortTest class.
  * Calls constructor for the asynPortDriver base class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] maxPoints The maximum  number of points in the volt and time arrays */
asynPortTest::asynPortTest(const char *portName, const char *echoPortName) 
   : asynPortDriver(portName, 
                    1, /* maxAddr */ 
                    asynInt32Mask | asynFloat64Mask | asynOctetMask | asynDrvUserMask, /* Interface mask */
                    asynInt32Mask | asynFloat64Mask | asynOctetMask,  /* Interrupt mask */
                    ASYN_CANBLOCK, /* asynFlags.  This driver blocks and it is not multi-device*/
                    1, /* Autoconnect */
                    0, /* Default priority */
                    0) /* Default stack size*/    
{
    //const char *functionName = "asynPortTest";

    pasynOctetSyncIO->connect(echoPortName, 0, &pasynUserEcho, NULL);
    createParam(Int32DataString,     asynParamInt32,        &Int32Data);
    createParam(Float64DataString,   asynParamFloat64,      &Float64Data);
    createParam(OctetDataString,     asynParamOctet,        &OctetData);    
}


/* Configuration routine.  Called directly, or from the iocsh function below */

extern "C" {

/** EPICS iocsh callable function to call constructor for the asynPortTest class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] echoPortName */
int asynPortTestConfigure(const char *portName, const char *echoPortName)
{
    new asynPortTest(portName, echoPortName);
    return(asynSuccess);
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName",    iocshArgString};
static const iocshArg initArg1 = { "echoPortname",iocshArgString};
static const iocshArg * const initArgs[] = {&initArg0,
                                            &initArg1};
static const iocshFuncDef initFuncDef = {"asynPortTestConfigure",2,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    asynPortTestConfigure(args[0].sval, args[1].sval);
}

void asynPortTestRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(asynPortTestRegister);

}
