#include "asynDriver.h"
#define serialDriverType "serialDriver"
typedef struct serialDriver {
    asynStatus setBaud(asynUser *pasynUser,int value);
    asynStatus getBaud(asynUser *pasynUser);
    asynStatus setStopBits(asynUser *pasynUser,int value);
    asynStatus getStopBits(asynUser *pasynUser);
    asynStatus setBitsPerChar(asynUser *pasynUser,int value);
    asynStatus getBitsPerChar(asynUser *pasynUser);
    asynStatus setParity(asynUser *pasynUser,int value);
    asynStatus getParity(asynUser *pasynUser);
    asynStatus setFlowControl(asynUser *pasynUser,int value);
    asynStatus getFlowControl(asynUser *pasynUser);
}serialDriver;

