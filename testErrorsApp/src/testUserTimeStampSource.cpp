#include <iocsh.h>
#include "asynDriver.h"
#include <epicsExport.h>


// This function demonstrates using a user-define time stamp source
// It simply returns the current time but with the nsec field set to 0, so that record timestamps
// can be checked to see that the user-defined function is indeed being called.

static void myTimeStampSource(void *userPvt, epicsTimeStamp *pTimeStamp)
{
    epicsTimeGetCurrent(pTimeStamp);
    pTimeStamp->nsec = 0;
}

static void registerMyTimeStampSource(const char *portName)
{
    asynUser *pasynUser;
    asynStatus status;
    
    pasynUser = pasynManager->createAsynUser(0, 0);
    status = pasynManager->connectDevice(pasynUser, portName, 0);
    if (status) {
        printf("registerMyTimeStampSource, cannot connect to port %s\n", portName);
        return;
    }
    pasynManager->registerTimeStampSource(pasynUser, 0, myTimeStampSource);
}

static void unregisterMyTimeStampSource(const char *portName)
{
    asynUser *pasynUser;
    asynStatus status;
    
    pasynUser = pasynManager->createAsynUser(0, 0);
    status = pasynManager->connectDevice(pasynUser, portName, 0);
    if (status) {
        printf("unregisterMyTimeStampSource, cannot connect to port %s\n", portName);
        return;
    }
    pasynManager->unregisterTimeStampSource(pasynUser);
}


extern "C" {

/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName",iocshArgString};
static const iocshArg * const initArgs[] = {&initArg0};
static const iocshFuncDef registerFuncDef = {"registerMyTimeStampSource",1,initArgs};
static const iocshFuncDef unregisterFuncDef = {"unregisterMyTimeStampSource",1,initArgs};
static void registerCallFunc(const iocshArgBuf *args)
{
    registerMyTimeStampSource(args[0].sval);
}

static void unregisterCallFunc(const iocshArgBuf *args)
{
    unregisterMyTimeStampSource(args[0].sval);
}

void testUserTimeStampSourceRegister(void)
{
    iocshRegister(&registerFuncDef, registerCallFunc);
    iocshRegister(&unregisterFuncDef, unregisterCallFunc);
}

epicsExportRegistrar(testUserTimeStampSourceRegister);

}
