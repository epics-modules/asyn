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

/** Pushes numUpdates new values to every channel, one round at a time,
  * with no sleeps in between, to simulate a driver receiving data faster
  * than the records connected to it can be processed. Each round calls
  * setIntegerParam() for every channel followed by a single
  * callParamCallbacks(), which is what actually delivers the interrupt
  * callback (and therefore the scanIoRequest()) to every I/O Intr scanned
  * record connected to a channel that changed. */
void testQueueOverflow::burst(int numUpdates)
{
    static const char *functionName = "burst";
    int round, chan;
    epicsInt32 value = 0;

    lock();
    for (round = 0; round < numUpdates; round++) {
        for (chan = 0; chan < numChannels_; chan++) {
            setIntegerParam(channelParam_[chan], value);
        }
        value++;
        callParamCallbacks();
    }
    unlock();
    printf("%s:%s: port=%s pushed %d rounds * %d channels = %d interrupt callbacks\n",
           driverName, functionName, portName, numUpdates, numChannels_,
           numUpdates * numChannels_);
}

extern "C" {

int testQueueOverflowConfigure(const char *portName, int numChannels)
{
    new testQueueOverflow(portName, numChannels);
    return asynSuccess;
}

int testQueueOverflowBurst(const char *portName, int numUpdates)
{
    testQueueOverflow *pDriver = findDerivedAsynPortDriver<testQueueOverflow>(portName);
    if (!pDriver) {
        printf("testQueueOverflowBurst: port %s not found\n", portName);
        return asynError;
    }
    pDriver->burst(numUpdates);
    return asynSuccess;
}

/* EPICS iocsh shell commands */

static const iocshArg configureArg0 = { "portName", iocshArgString };
static const iocshArg configureArg1 = { "numChannels", iocshArgInt };
static const iocshArg * const configureArgs[] = { &configureArg0, &configureArg1 };
static const iocshFuncDef configureFuncDef = {
    "testQueueOverflowConfigure", 2, configureArgs,
    "Create a testQueueOverflow asyn port with numChannels asynInt32 parameters" };
static void configureCallFunc(const iocshArgBuf *args)
{
    testQueueOverflowConfigure(args[0].sval, args[1].ival);
}

static const iocshArg burstArg0 = { "portName", iocshArgString };
static const iocshArg burstArg1 = { "numUpdates", iocshArgInt };
static const iocshArg * const burstArgs[] = { &burstArg0, &burstArg1 };
static const iocshFuncDef burstFuncDef = {
    "testQueueOverflowBurst", 2, burstArgs,
    "Push numUpdates new values to every channel of portName back-to-back" };
static void burstCallFunc(const iocshArgBuf *args)
{
    testQueueOverflowBurst(args[0].sval, args[1].ival);
}

void testQueueOverflowRegister(void)
{
    iocshRegister(&configureFuncDef, configureCallFunc);
    iocshRegister(&burstFuncDef, burstCallFunc);
}

epicsExportRegistrar(testQueueOverflowRegister);

}
