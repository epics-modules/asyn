/*
 * testQueueOverflow.cpp
 *
 * See testQueueOverflow.h for a description of what this driver demonstrates.
 *
 * Author: Ralph Lange
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <epicsString.h>
#include <epicsExit.h>
#include <epicsThread.h>
#include <iocsh.h>

#include "testQueueOverflow.h"
#include <epicsExport.h>

static const char *driverName = "testQueueOverflow";

testQueueOverflow::testQueueOverflow(const char *portName, int numChannels)
   : asynPortDriver(portName,
                    1, /* maxAddr */
                    asynInt32Mask | asynDrvUserMask, /* Interface mask */
                    asynInt32Mask,                   /* Interrupt mask */
                    0, /* asynFlags: does not block, not multi-device */
                    1, /* Autoconnect */
                    0, /* Default priority */
                    0) /* Default stack size */
{
    int chan;
    char paramName[16];

    numChannels_ = (numChannels > 0) ? numChannels : 1;
    channelParam_ = (int *)calloc(numChannels_, sizeof(int));

    for (chan = 0; chan < numChannels_; chan++) {
        epicsSnprintf(paramName, sizeof(paramName), "CHANNEL%d", chan);
        createParam(paramName, asynParamInt32, &channelParam_[chan]);
        setIntegerParam(channelParam_[chan], 0);
    }
}

testQueueOverflow::~testQueueOverflow()
{
    free(channelParam_);
}

/** Pushes numUpdates new values to every channel, one round at a time, to
  * simulate a driver receiving data faster than the records connected to it
  * can be processed. Each round calls setIntegerParam() for every channel
  * followed by a single callParamCallbacks(), which is what actually delivers
  * the interrupt callback (and therefore the scanIoRequest()) to every
  * I/O Intr scanned record connected to a channel that changed.
  *
  * With delay = 0 the rounds are pushed back-to-back, so the records cannot
  * keep up and the per-record ring buffers overflow. With a delay longer than
  * the record processing time each round is fully consumed before the next
  * arrives, which exercises the empty -> refill transition of the ring buffer
  * repeatedly: if a value were ever pushed without a process request being
  * (re-)issued, the ring buffer would back up and start reporting overflows. */
void testQueueOverflow::burst(int numUpdates, double delay)
{
    static const char *functionName = "burst";
    int round, chan;
    epicsInt32 value = 0;

    for (round = 0; round < numUpdates; round++) {
        lock();
        for (chan = 0; chan < numChannels_; chan++) {
            setIntegerParam(channelParam_[chan], value);
        }
        value++;
        callParamCallbacks();
        unlock();
        if (delay > 0.0) epicsThreadSleep(delay);
    }
    printf("%s:%s: port=%s pushed %d rounds * %d channels = %d interrupt callbacks"
           " (delay %f s)\n",
           driverName, functionName, portName, numUpdates, numChannels_,
           numUpdates * numChannels_, delay);
}

extern "C" {

int testQueueOverflowConfigure(const char *portName, int numChannels)
{
    new testQueueOverflow(portName, numChannels);
    return asynSuccess;
}

int testQueueOverflowBurst(const char *portName, int numUpdates, double delay)
{
    testQueueOverflow *pDriver = findDerivedAsynPortDriver<testQueueOverflow>(portName);
    if (!pDriver) {
        printf("testQueueOverflowBurst: port %s not found\n", portName);
        return asynError;
    }
    pDriver->burst(numUpdates, delay);
    return asynSuccess;
}

/* EPICS iocsh shell commands */

static const iocshArg configureArg0 = { "portName", iocshArgString };
static const iocshArg configureArg1 = { "numChannels", iocshArgInt };
static const iocshArg * const configureArgs[] = { &configureArg0, &configureArg1 };
#ifdef IOCSHFUNCDEF_HAS_USAGE
static const char configureUsage[] =
    "Create a testQueueOverflow asyn port with numChannels asynInt32 parameters\n";
#endif
static const iocshFuncDef configureFuncDef = {
    "testQueueOverflowConfigure", 2, configureArgs
#ifdef IOCSHFUNCDEF_HAS_USAGE
    , configureUsage
#endif
};
static void configureCallFunc(const iocshArgBuf *args)
{
    testQueueOverflowConfigure(args[0].sval, args[1].ival);
}

static const iocshArg burstArg0 = { "portName", iocshArgString };
static const iocshArg burstArg1 = { "numUpdates", iocshArgInt };
static const iocshArg burstArg2 = { "delay", iocshArgDouble };
static const iocshArg * const burstArgs[] = { &burstArg0, &burstArg1, &burstArg2 };
#ifdef IOCSHFUNCDEF_HAS_USAGE
static const char burstUsage[] =
    "Push numUpdates new values to every channel of portName, pausing delay\n"
    "seconds between rounds (0 = back-to-back)\n";
#endif
static const iocshFuncDef burstFuncDef = {
    "testQueueOverflowBurst", 3, burstArgs
#ifdef IOCSHFUNCDEF_HAS_USAGE
    , burstUsage
#endif
};
static void burstCallFunc(const iocshArgBuf *args)
{
    testQueueOverflowBurst(args[0].sval, args[1].ival, args[2].dval);
}

void testQueueOverflowRegister(void)
{
    iocshRegister(&configureFuncDef, configureCallFunc);
    iocshRegister(&burstFuncDef, burstCallFunc);
}

epicsExportRegistrar(testQueueOverflowRegister);

}
