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
#include <epicsExport.h>
#include <iocsh.h>

#include "asynPortDriver.h"

#define FREQUENCY 1000       /* Frequency in Hz */
#define AMPLITUDE 1.0        /* Plus and minus peaks of sin wave */
#define NUM_DIVISIONS 10     /* Number of scope divisions in X and Y */
#define MIN_UPDATE_TIME 0.02 /* Minimum update time, to prevent CPU saturation */

/** Class that demonstrates the use of the asynPortDriver base class to greatly simplify the task
  * of writing an asyn port driver.
  * This class does a simple simulation of a digital oscilloscope.  It computes a waveform, computes
  * statistics on the waveform, and does callbacks with the statistics and the waveform data itself. 
  * I have made the methods of this class public in order to generate doxygen documentation for them,
  * but they should really all be private. */
class testAsynPortDriver : public asynPortDriver {
public:
    testAsynPortDriver(const char *portName, int maxArraySize);
                 
    /* These are the methods that we override from asynPortDriver */
    virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
    virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
    virtual asynStatus readFloat64Array(asynUser *pasynUser, epicsFloat64 *value,
                                        size_t nElements, size_t *nIn);
    virtual asynStatus drvUserCreate(asynUser *pasynUser, const char *drvInfo, 
                                     const char **pptypeName, size_t *psize);
                                     
    /* These are the methods that are new to this class */
    void simTask(void);
    
private:
    /* Our data */
    epicsEventId eventId;
    epicsFloat64 *pData;
    epicsFloat64 *pTimeBase;
};

/** Enum values are used for pasynUser->reason values, and are the index into the parameter library. */
typedef enum {
    P_Run,                /* asynInt32,    r/w */
    P_MaxPoints,          /* asynInt32,    r/o */
    P_TimePerDivision,    /* asynFloat64,  r/w */
    P_VoltsPerDivision,   /* asynFloat64,  r/w */
    P_VoltOffset,         /* asynFloat64,  r/w */
    P_TriggerDelay,       /* asynFloat64,  r/w */
    P_NoiseAmplitude,     /* asynFloat64,  r/w */
    P_UpdateTime,         /* asynFloat64,  r/w */
    P_Waveform,           /* asynFloat64Array,  r/o */
    P_TimeBase,           /* asynFloat64Array,  r/o */
    P_MinValue,           /* asynFloat64,  r/o */
    P_MaxValue,           /* asynFloat64,  r/o */
    P_MeanValue           /* asynFloat64,  r/o */
} testParams;

/** The command strings are the drvInfo argument for asyn device support links
  * The asynDrvUser interface in this driver parses these strings and puts the
  * corresponding enum value in pasynUser->reason */
static asynParamString_t driverParamString[] = {
    {P_Run,              "RUN"            },
    {P_MaxPoints,        "MAX_POINTS"     },
    {P_TimePerDivision,  "TIME_PER_DIV"   },
    {P_VoltsPerDivision, "VOLTS_PER_DIV"  },
    {P_VoltOffset,       "VOLT_OFFSET"    },
    {P_TriggerDelay,     "TRIGGER_DELAY"  },
    {P_NoiseAmplitude,   "NOISE_AMPLITUDE"},
    {P_UpdateTime,       "UPDATE_TIME"    },
    {P_Waveform,         "WAVEFORM"       },
    {P_TimeBase,         "TIME_BASE"      },
    {P_MinValue,         "MIN_VALUE"      },
    {P_MaxValue,         "MAX_VALUE"      },
    {P_MeanValue,        "MEAN_VALUE"     }
};

#define NUM_DRIVER_PARAMS (sizeof(driverParamString)/sizeof(driverParamString[0]))

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
    const char* functionName = "writeInt32";

    /* Set the parameter in the parameter library. */
    status = (asynStatus) setIntegerParam(function, value);

    switch(function) {
        case P_Run:
            /* If run was set then wake up the simulation task */
            if (value) epicsEventSignal(this->eventId);
            break;
        default:
            /* All other parameters just get set in parameter list, no need to
             * act on them here */
            break;
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
  * This function sends a signal to the simTask thread if the value of P_UpdateTime has changed.
  * For all  parameters it  sets the value in the parameter library and calls any registered callbacks.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus testAsynPortDriver::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    int run;
    const char* functionName = "writeFloat64";

    /* Set the parameter in the parameter library. */
    status = (asynStatus) setDoubleParam(function, value);

    switch(function) {
        case P_UpdateTime:
            /* Make sure the update time is valid. If not change it and put back in parameter library */
            if (value < MIN_UPDATE_TIME) {
                value = MIN_UPDATE_TIME;
                setDoubleParam(P_UpdateTime, value);
            }
            /* If the update time has changed and we are running then wake up the simulation task */
            getIntegerParam(P_Run, &run);
            if (run) epicsEventSignal(this->eventId);
            break;
        default:
            /* All other parameters just get set in parameter list, no need to
             * act on them here */
            break;
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
    switch(function) {
        case P_Waveform:
            memcpy(value, this->pData, ncopy*sizeof(epicsFloat64));
            *nIn = ncopy;
            break;
        case P_TimeBase:
            memcpy(value, this->pTimeBase, ncopy*sizeof(epicsFloat64));
            *nIn = ncopy;
            break;
        default:
            break;
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
    

/** Called by asynManager to pass a pasynUser structure and drvInfo string to the driver; 
  * assigns pasynUser->reason to one of the testParams enum value based on the value of the drvInfo string.
  * Simply calls asynPortDriver::drvUserCreateParam with the parameter table for this driver.
  * \param[in] pasynUser pasynUser structure that driver modifies
  * \param[in] drvInfo String containing information about what driver function is being referenced
  * \param[out] pptypeName Location in which driver puts a copy of drvInfo.
  * \param[out] psize Location where driver puts size of param.
  * \return Returns asynSuccess if a matching string was found, asynError if not found. */
asynStatus testAsynPortDriver::drvUserCreate(asynUser *pasynUser,
                                       const char *drvInfo, 
                                       const char **pptypeName, size_t *psize)
{
    //const char *functionName = "drvUserCreate";
    
    return this->drvUserCreateParam(pasynUser, drvInfo, pptypeName, psize, 
                                    driverParamString, NUM_DRIVER_PARAMS);
}

/** Constructor for the testAsynPortDriver class.
  * Calls constructor for the asynPortDriver base class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] maxPoints The maximum  number of points in the volt and time arrays */
testAsynPortDriver::testAsynPortDriver(const char *portName, int maxPoints) 
   : asynPortDriver(portName, 
                    1, /* maxAddr */ 
                    NUM_DRIVER_PARAMS,
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

