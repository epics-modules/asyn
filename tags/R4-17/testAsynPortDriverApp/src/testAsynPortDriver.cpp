/*
 * testAsynPortDriver.cpp
 * 
 * Asyn driver that inherits from the asynPortDriver class to demonstrate its use.
 * It simulates a digital scope looking at a 1kHz 1000-point noisy sine wave.  Controls are
 * provided for time/division, volts/division, volt offset, trigger delay, noise amplitude, update time,
 * and run/stop.
 * Readbacks are provides for the waveform data, min, max and mean values.
 *
 * Author: Mark Rivers
 *
 * Created Feb. 5, 2009
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>

#include <epicsTypes.h>
#include <epicsTime.h>
#include <epicsThread.h>
#include <epicsString.h>
#include <epicsTimer.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <iocsh.h>

#include "testAsynPortDriver.h"
#include <epicsExport.h>

#define FREQUENCY 1000       /* Frequency in Hz */
#define AMPLITUDE 1.0        /* Plus and minus peaks of sin wave */
#define NUM_DIVISIONS 10     /* Number of scope divisions in X and Y */
#define MIN_UPDATE_TIME 0.02 /* Minimum update time, to prevent CPU saturation */

static const char *driverName="testAsynPortDriver";


void simTask(void *drvPvt)
{
    testAsynPortDriver *pPvt = (testAsynPortDriver *)drvPvt;
    
    pPvt->simTask();
}

/** Simulation task that runs as a separate thread.  When the P_Run parameter is set to 1
  * to rub the simulation it computes a 1 kHz sine wave with 1V amplitude and user-controllable
  * noise, and displays it on
  * a simulated scope.  It computes waveforms for the X (time) and Y (volt) axes, and computes
  * statistics about the waveform. */
void testAsynPortDriver::simTask(void)
{
    /* This thread computes the waveform and does callbacks with it */

    double timePerDivision, voltsPerDivision, voltOffset, triggerDelay, noiseAmplitude;
    double updateTime, minValue, maxValue, meanValue;
    double time, timeStep;
    double noise, yScale;
    int run, i, maxPoints;
    double pi=4.0*atan(1.0);
    
    /* Loop forever */    
    while (1) {
        getDoubleParam(P_UpdateTime, &updateTime);
        getIntegerParam(P_Run, &run);
        if (run) epicsEventWaitWithTimeout(this->eventId, updateTime);
        else     epicsEventWait(this->eventId);
        /* run could have changed while we were waiting */
        getIntegerParam(P_Run, &run);
        if (!run) continue;
        getIntegerParam(P_MaxPoints,        &maxPoints);
        getDoubleParam (P_TimePerDivision,  &timePerDivision);
        getDoubleParam (P_VoltsPerDivision, &voltsPerDivision);
        getDoubleParam (P_VoltOffset,       &voltOffset);
        getDoubleParam (P_TriggerDelay,     &triggerDelay);
        getDoubleParam (P_NoiseAmplitude,   &noiseAmplitude);
        time = triggerDelay;
        timeStep = timePerDivision * NUM_DIVISIONS / maxPoints;
        minValue = 1e6;
        maxValue = -1e6;
        meanValue = 0.;
    
        yScale = 1.0 / voltsPerDivision;
        for (i=0; i<maxPoints; i++) {
            noise = noiseAmplitude * (rand()/(double)RAND_MAX - 0.5);
            pData[i] = AMPLITUDE * (sin(time*FREQUENCY*2*pi)) + noise;
            /* Compute statistics before doing the yOffset and yScale */
            if (pData[i] < minValue) minValue = pData[i];
            if (pData[i] > maxValue) maxValue = pData[i];
            meanValue += pData[i];
            pData[i] = NUM_DIVISIONS/2 + yScale * (voltOffset + pData[i]);
            time += timeStep;
        }
        meanValue = meanValue/maxPoints;
        setDoubleParam(P_MinValue, minValue);
        setDoubleParam(P_MaxValue, maxValue);
        setDoubleParam(P_MeanValue, meanValue);
        callParamCallbacks();
        doCallbacksFloat64Array(pData, maxPoints, P_Waveform, 0);
    }
}

/** Called when asyn clients call pasynInt32->write().
  * This function sends a signal to the simTask thread if the value of P_Run has changed.
  * For all parameters it sets the value in the parameter library and calls any registered callbacks..
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus testAsynPortDriver::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *paramName;
    const char* functionName = "writeInt32";

    /* Set the parameter in the parameter library. */
    status = (asynStatus) setIntegerParam(function, value);
    
    /* Fetch the parameter string name for possible use in debugging */
    getParamName(function, &paramName);

    if (function == P_Run) {
        /* If run was set then wake up the simulation task */
        if (value) epicsEventSignal(this->eventId);
    } else {
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

/** Called when asyn clients call pasynFloat64->write().
  * This function sends a signal to the simTask thread if the value of P_UpdateTime has changed.
  * For all  parameters it  sets the value in the parameter library and calls any registered callbacks.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus testAsynPortDriver::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    int run;
    const char *paramName;
    const char* functionName = "writeFloat64";

    /* Set the parameter in the parameter library. */
    status = (asynStatus) setDoubleParam(function, value);

    /* Fetch the parameter string name for possible use in debugging */
    getParamName(function, &paramName);

    if (function == P_UpdateTime) {
        /* Make sure the update time is valid. If not change it and put back in parameter library */
        if (value < MIN_UPDATE_TIME) {
            value = MIN_UPDATE_TIME;
            setDoubleParam(P_UpdateTime, value);
        }
        /* If the update time has changed and we are running then wake up the simulation task */
        getIntegerParam(P_Run, &run);
        if (run) epicsEventSignal(this->eventId);
    } else {
        /* All other parameters just get set in parameter list, no need to
         * act on them here */
    }
    
    /* Do callbacks so higher layers see any changes */
    status = (asynStatus) callParamCallbacks();
    
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


/** Called when asyn clients call pasynFloat64Array->read().
  * Returns the value of the P_Waveform or P_TimeBase arrays.  
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Pointer to the array to read.
  * \param[in] nElements Number of elements to read.
  * \param[out] nIn Number of elements actually read. */
asynStatus testAsynPortDriver::readFloat64Array(asynUser *pasynUser, epicsFloat64 *value, 
                                         size_t nElements, size_t *nIn)
{
    int function = pasynUser->reason;
    size_t ncopy;
    asynStatus status = asynSuccess;
    const char *functionName = "readFloat64Array";

    getIntegerParam(P_MaxPoints, (epicsInt32 *)&ncopy);
    if (nElements < ncopy) ncopy = nElements;
    if (function == P_Waveform) {
        memcpy(value, this->pData, ncopy*sizeof(epicsFloat64));
        *nIn = ncopy;
    }
    else if (function == P_TimeBase) {
        memcpy(value, this->pTimeBase, ncopy*sizeof(epicsFloat64));
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
    


/** Constructor for the testAsynPortDriver class.
  * Calls constructor for the asynPortDriver base class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] maxPoints The maximum  number of points in the volt and time arrays */
testAsynPortDriver::testAsynPortDriver(const char *portName, int maxPoints) 
   : asynPortDriver(portName, 
                    1, /* maxAddr */ 
                    NUM_SCOPE_PARAMS,
                    asynInt32Mask | asynFloat64Mask | asynFloat64ArrayMask | asynDrvUserMask, /* Interface mask */
                    asynInt32Mask | asynFloat64Mask | asynFloat64ArrayMask,  /* Interrupt mask */
                    0, /* asynFlags.  This driver does not block and it is not multi-device, so flag is 0 */
                    1, /* Autoconnect */
                    0, /* Default priority */
                    0) /* Default stack size*/    
{
    asynStatus status;
    int i;
    const char *functionName = "testAsynPortDriver";

    /* Make sure maxPoints is positive */
    if (maxPoints < 1) maxPoints = 100;
    
    /* Allocate the waveform array */
    pData = (epicsFloat64 *)calloc(maxPoints, sizeof(epicsFloat64));

    /* Allocate the time base array */
    pTimeBase = (epicsFloat64 *)calloc(maxPoints, sizeof(epicsFloat64));
    /* Set the time base array */
    for (i=0; i<maxPoints; i++) pTimeBase[i] = (double)i / (maxPoints-1) * NUM_DIVISIONS;
    
    this->eventId = epicsEventCreate(epicsEventEmpty);
    createParam(P_RunString,                asynParamInt32,         &P_Run);
    createParam(P_MaxPointsString,          asynParamInt32,         &P_MaxPoints);
    createParam(P_TimePerDivisionString,    asynParamFloat64,       &P_TimePerDivision);
    createParam(P_VoltsPerDivisionString,   asynParamFloat64,       &P_VoltsPerDivision);
    createParam(P_VoltOffsetString,         asynParamFloat64,       &P_VoltOffset);
    createParam(P_TriggerDelayString,       asynParamFloat64,       &P_TriggerDelay);
    createParam(P_NoiseAmplitudeString,     asynParamFloat64,       &P_NoiseAmplitude);
    createParam(P_UpdateTimeString,         asynParamFloat64,       &P_UpdateTime);
    createParam(P_WaveformString,           asynParamFloat64Array,  &P_Waveform);
    createParam(P_TimeBaseString,           asynParamFloat64Array,  &P_TimeBase);
    createParam(P_MinValueString,           asynParamFloat64,       &P_MinValue);
    createParam(P_MaxValueString,           asynParamFloat64,       &P_MaxValue);
    createParam(P_MeanValueString,          asynParamFloat64,       &P_MeanValue);
    
    /* Set the initial values of some parameters */
    setIntegerParam(P_MaxPoints,         maxPoints);
    setIntegerParam(P_Run,               0);
    setDoubleParam (P_VoltsPerDivision,  0.2);
    setDoubleParam (P_VoltOffset,        0.0);
    setDoubleParam (P_TriggerDelay,      0.0);
    setDoubleParam (P_TimePerDivision,   0.001);
    setDoubleParam (P_UpdateTime,        0.5);
    setDoubleParam (P_NoiseAmplitude,    0.1);
    setDoubleParam (P_MinValue,          0.0);
    setDoubleParam (P_MaxValue,          0.0);
    setDoubleParam (P_MeanValue,         0.0);

    /* Create the thread that computes the waveforms in the background */
    status = (asynStatus)(epicsThreadCreate("testAsynPortDriverTask",
                          epicsThreadPriorityMedium,
                          epicsThreadGetStackSize(epicsThreadStackMedium),
                          (EPICSTHREADFUNC)::simTask,
                          this) == NULL);
    if (status) {
        printf("%s:%s: epicsThreadCreate failure\n", driverName, functionName);
        return;
    }
}


/* Configuration routine.  Called directly, or from the iocsh function below */

extern "C" {

/** EPICS iocsh callable function to call constructor for the testAsynPortDriver class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] maxPoints The maximum  number of points in the volt and time arrays */
int testAsynPortDriverConfigure(const char *portName, int maxPoints)
{
    new testAsynPortDriver(portName, maxPoints);
    return(asynSuccess);
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName",iocshArgString};
static const iocshArg initArg1 = { "max points",iocshArgInt};
static const iocshArg * const initArgs[] = {&initArg0,
                                            &initArg1};
static const iocshFuncDef initFuncDef = {"testAsynPortDriverConfigure",2,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    testAsynPortDriverConfigure(args[0].sval, args[1].ival);
}

void testAsynPortDriverRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(testAsynPortDriverRegister);

}

