/* asynFloat64Callback.h

    10-July-2004  MLR  Mark Rivers

*/

#ifndef asynFloat64CallbackH
#define asynFloat64CallbackH

#include <asynDriver.h>
#include <epicsTypes.h>

#define asynFloat64CallbackType "asynFloat64Callback"

typedef void (*asynFloat64DataCallback)(void *drvPvt, epicsFloat64 data);
typedef void (*asynFloat64IntervalCallback)(void *drvPvt, double seconds);

typedef struct asynFloat64Callback {
    double     (*setCallbackInterval)(void *drvPvt, asynUser *pasynUser,
                                      double seconds);
    double     (*getCallbackInterval)(void *drvPvt, asynUser *pasynUser);
    asynStatus (*registerCallbacks)(void *drvPvt, asynUser *pasynUser,
                                   asynFloat64DataCallback dataCallback, 
                                   asynFloat64IntervalCallback intervalCallback,
                                   void *pvt);
} asynFloat64Callback;

#endif /* asynFloat64CallbackH*/
