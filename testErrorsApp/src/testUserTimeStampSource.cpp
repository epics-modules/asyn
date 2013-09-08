#include <iocsh.h>
#include <epicsExport.h>
#include "asynPortDriver.h"


// This function demonstrates using a user-define time stamp source
// It simply returns the current time but with the nsec field set to 0, so that record timestamps
// can be checked to see that the user-defined function is indeed being called.

static void myTimeStampSource(void *userPvt, epicsTimeStamp *pTimeStamp)
{
    epicsTimeGetCurrent(pTimeStamp);
    pTimeStamp->nsec = 0;
}

static void testUserTimeStampSource(const char *portName)
{
    asynPortDriver *pPort=0;
    pPort = (asynPortDriver *)findAsynPortDriver(portName);
    if (!pPort) {
        printf("testUserTimeStampSource, cannot find port %s\n", portName);
        return;
    }
    pPort->registerUserTimeStampSource(0, myTimeStampSource);
}


extern "C" {

/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName",iocshArgString};
static const iocshArg * const initArgs[] = {&initArg0};
static const iocshFuncDef initFuncDef = {"testUserTimeStampSource",1,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    testUserTimeStampSource(args[0].sval);
}

void testUserTimeStampSourceRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(testUserTimeStampSourceRegister);

}
