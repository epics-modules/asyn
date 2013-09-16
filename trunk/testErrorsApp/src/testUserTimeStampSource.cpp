#include <epicsTime.h>
#include <registryFunction.h>
#include <epicsExport.h>

// This function demonstrates using a user-define time stamp source
// It simply returns the current time but with the nsec field set to 0, so that record timestamps
// can be checked to see that the user-defined function is indeed being called.

static void myTimeStampSource(void *userPvt, epicsTimeStamp *pTimeStamp)
{
    epicsTimeGetCurrent(pTimeStamp);
    pTimeStamp->nsec = 0;
}

extern "C" {
epicsRegisterFunction(myTimeStampSource);
}
