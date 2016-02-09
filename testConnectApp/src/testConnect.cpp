/*
 * testConnect.cpp
 * 
 * Asyn driver that inherits from the asynPortDriver class to test connection handling
 *
 * Author: Mark Rivers
 *
 * Created June 2, 2012
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <epicsThread.h>
#include <epicsString.h>
#include <iocsh.h>
#include <asynPortDriver.h>
#include <asynOctetSyncIO.h>

#include <epicsExport.h>

class testConnect : public asynPortDriver {
public:
    testConnect(const char *portName, const char *IPPortName, const char *outputString);
    void pollerTask(void);
private:
    asynUser *pasynUserIPPort_;
    const char *outputString_;
};

static const char *driverName="testConnect";

#define POLLER_PERIOD 1
#define NUM_PARAMS 0
#define MAX_RESPONSE_LEN 256

static void pollerTask(void *drvPvt);

testConnect::testConnect(const char *portName, const char *IPPortName, const char *outputString) 
   : asynPortDriver(portName, 
                    1, /* maxAddr */ 
                    NUM_PARAMS,
                     /* Interface mask */
                    asynOctetMask,
                    /* Interrupt mask */
                    0,
                    ASYN_CANBLOCK, /* asynFlags.  This driver does block and it is not multi-device*/
                    1, /* Autoconnect */
                    0, /* Default priority */
                    0) /* Default stack size*/    
{
    asynStatus status;
    const char *functionName = "testConnect";
    
    outputString_ = epicsStrDup(outputString);
    
    /* Connect to the port */
    status = pasynOctetSyncIO->connect(IPPortName, 0, &pasynUserIPPort_, NULL);
    if (status) {
        printf("%s:%s: pasynOctetSyncIO->connect failure, status=%d\n", driverName, functionName, status);
        return;
    }
    
    /* Create the thread that computes the waveforms in the background */
    status = (asynStatus)(epicsThreadCreate("testConnectTask",
                          epicsThreadPriorityMedium,
                          epicsThreadGetStackSize(epicsThreadStackMedium),
                          (EPICSTHREADFUNC)::pollerTask,
                          this) == NULL);
    if (status) {
        printf("%s:%s: epicsThreadCreate failure, status=%d\n", driverName, functionName, status);
        return;
    }
}



static void pollerTask(void *drvPvt)
{
    testConnect *pPvt = (testConnect *)drvPvt;
    
    pPvt->pollerTask();
}

/** Poller task that runs as a separate thread. */
void testConnect::pollerTask(void)
{
    asynStatus status;
    char response[MAX_RESPONSE_LEN] = "";
    size_t numWrite, numRead;
    int isConnected;
    int eomReason;
    static const char *functionName = "pollerTask";
    
    /* Loop forever */    
    while (1) {
        lock();
        status = pasynManager->isConnected(pasynUserIPPort_, &isConnected);
        if (status) {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: error calling pasynManager->isConnected, status=%d, error=%s\n", 
                driverName, functionName, status, pasynUserIPPort_->errorMessage);
        }
        asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER,
            "%s:%s: isConnected = %d\n", driverName, functionName, isConnected);
        
        status = pasynOctetSyncIO->writeRead(pasynUserIPPort_, outputString_, strlen(outputString_),
                                             response, sizeof(response), 
                                             1.0, &numWrite, &numRead, &eomReason);
        if (status) {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: error calling pasynOctetSyncIO->writeRead, status=%d, error=%s\n", 
                driverName, functionName, status, pasynUserIPPort_->errorMessage);
        }
        else {
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER,
                "%s:%s: numWrite=%ld, wrote: %s, numRead=%ld, response=%s\n", 
                driverName, functionName, (long)numWrite, outputString_, (long)numRead, response);
        }
        unlock();
        epicsThreadSleep(POLLER_PERIOD);
    }
}

extern "C" {
int testConnectConfigure(const char *portName, const char *IPPortName, const char *outputString)
{
    new testConnect(portName, IPPortName, outputString);
    return(asynSuccess);
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName",iocshArgString};
static const iocshArg initArg1 = { "IPPortName",iocshArgString};
static const iocshArg initArg2 = { "output string",iocshArgString};
static const iocshArg * const initArgs[] = {&initArg0, &initArg1, &initArg2};
static const iocshFuncDef initFuncDef = {"testConnectConfigure",3,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    testConnectConfigure(args[0].sval, args[1].sval, args[2].sval);
}

void testConnectRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(testConnectRegister);
}
