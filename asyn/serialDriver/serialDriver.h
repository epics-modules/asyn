#include "asynDriver.h"
#define serialDriverType "serialDriver"
typedef struct serialDriver {
    asynStatus setBaud(void *pdrvPvt,asynUser *pasynUser,int value);
    asynStatus getBaud(void *pdrvPvt,asynUser *pasynUser);
    asynStatus setStopBits(void *pdrvPvt,asynUser *pasynUser,int value);
    asynStatus getStopBits(void *pdrvPvt,asynUser *pasynUser);
    asynStatus setBitsPerChar(void *pdrvPvt,asynUser *pasynUser,int value);
    asynStatus getBitsPerChar(void *pdrvPvt,asynUser *pasynUser);
    asynStatus setParity(void *pdrvPvt,asynUser *pasynUser,int value);
    asynStatus getParity(void *pdrvPvt,asynUser *pasynUser);
    asynStatus setFlowControl(void *pdrvPvt,asynUser *pasynUser,int value);
    asynStatus getFlowControl(void *pdrvPvt,asynUser *pasynUser);
}serialDriver;

