/* asynFloat64Callback.h

    10-July-2004  MLR  Mark Rivers

*/

#ifndef asynFloat64CallbackH
#define asynFloat64CallbackH

#include <asynDriver.h>
#include <epicsTypes.h>

#define asynFloat64CallbackType "asynFloat64Callback"

typedef void (*asynFloat64Clbk)(void *drvPvt, epicsFloat64 readback);

typedef struct {
    double     (*setCallbackInterval)(void *drvPvt, asynUser *pasynUser,
                                      double seconds);
    double     (*getCallbackInterval)(void *drvPvt, asynUser *pasynUser);
    asynStatus (*registerCallback)   (void *drvPvt, asynUser *pasynUser,
                                      asynFloat64Clbk callback, void *pvt);
} asynFloat64Callback;

#endif /* asynFloat64CallbackH*/
